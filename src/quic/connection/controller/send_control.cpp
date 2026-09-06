#include <algorithm>
#include <cstring>
#include <quicx/common/metrics.h>
#include <string>

#include "common/log/log.h"
#include "common/metrics/metrics_std.h"
#include "common/qlog/qlog.h"

#include "quic/config.h"
#include "quic/congestion_control/congestion_control_factory.h"
#include "quic/connection/controller/send_control.h"
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"

namespace quicx {
namespace quic {

SendControl::SendControl(std::shared_ptr<common::ITimerScheduler> scheduler):
    max_ack_delay_(kMaxAckDelay),
    scheduler_(scheduler) {
    memset(pkt_num_largest_sent_, 0, sizeof(pkt_num_largest_sent_));
    memset(pkt_num_largest_acked_, 0, sizeof(pkt_num_largest_acked_));
    memset(largest_sent_time_, 0, sizeof(largest_sent_time_));

    // CC algorithm selection: read from config.h constant (kDefaultCongestionControl).
    // To switch algorithm, modify kDefaultCongestionControl in quic/config.h and rebuild.
    CongestionControlType cc_type = CongestionControlType::kReno;
    std::string v(kDefaultCongestionControl);
    if (v == "cubic") {
        cc_type = CongestionControlType::kCubic;
    } else if (v == "bbrv1" || v == "bbr" || v == "bbr1") {
        cc_type = CongestionControlType::kBbrV1;
    } else if (v == "bbrv2" || v == "bbr2") {
        cc_type = CongestionControlType::kBbrV2;
    } else if (v == "bbrv3" || v == "bbr3") {
        cc_type = CongestionControlType::kBbrV3;
    } else if (v == "reno") {
        cc_type = CongestionControlType::kReno;
    } else {
        LOG_WARN("SendControl: unknown CC algorithm \"%s\", falling back to reno", kDefaultCongestionControl);
    }
    LOG_INFO("SendControl: using congestion control: %s", v.c_str());
    congestion_control_ = CreateCongestionControl(cc_type);
}

void SendControl::OnPacketSend(uint64_t now, const std::shared_ptr<IPacket>& packet, uint32_t pkt_len) {
    OnPacketSend(now, packet, pkt_len, std::vector<StreamDataInfo>());
}

void SendControl::OnPacketSend(uint64_t now, const std::shared_ptr<IPacket>& packet, uint32_t pkt_len,
    const std::vector<StreamDataInfo>& stream_data) {
    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    LOG_DEBUG("SendControl::OnPacketSend: packet_number=%llu, ns=%d, frame_type_bit=%u, stream_data count=%zu",
        packet->GetPacketNumber(), ns, packet->GetFrameTypeBit(), stream_data.size());

    // Metrics: Packet transmitted
    Metrics::CounterInc(common::MetricsStd::QuicPacketsTx);

    if (pkt_num_largest_sent_[ns] > packet->GetPacketNumber()) {
        LOG_ERROR("invalid packet number. number:%llu", packet->GetPacketNumber());
        return;
    }
    pkt_num_largest_sent_[ns] = packet->GetPacketNumber();
    largest_sent_time_[ns] = common::UTCTimeMsec();

    // RFC 9002: Only ACK-eliciting packets count towards congestion control
    // ACK-only packets should NOT be accounted in bytes_in_flight
    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        LOG_DEBUG("SendControl::OnPacketSend: packet %llu is ACK-only, not counting in congestion control",
            packet->GetPacketNumber());
        return;
    }

    TrackPacketInCongestionControl(now, packet, pkt_len);
    EmitQlogPacketSent(packet, pkt_len);
    TrackPacketForRetransmission(packet, pkt_len, ns, stream_data);
}

void SendControl::TrackPacketInCongestionControl(
    uint64_t now, const std::shared_ptr<IPacket>& packet, uint32_t pkt_len) {
    // Count this packet in congestion control (bytes_in_flight)
    // BUGFIX: CC algorithms (BBR/Cubic) use microsecond-based internal timing.
    // The system clock (UTCTimeMsec) provides milliseconds; multiply by 1000
    // so that CC bandwidth/BDP/epoch calculations use the correct time scale.
    congestion_control_->OnPacketSent(SentPacketEvent{packet->GetPacketNumber(), pkt_len, now * 1000, false});

    // Track when we last sent ack-eliciting data for PTO timer
    last_ack_eliciting_sent_time_ = now;
}

void SendControl::EmitQlogPacketSent(const std::shared_ptr<IPacket>& packet, uint32_t pkt_len) {
    if (!qlog_trace_) {
        return;
    }

    common::PacketSentData qlog_data;
    qlog_data.packet_number = packet->GetPacketNumber();
    qlog_data.packet_type = packet->GetHeader()->GetPacketType();
    qlog_data.packet_size = pkt_len;
    // qlog draft-03: tag this packet with the UDP datagram it travels
    // in, so qvis can render Initial+Handshake (and similar) coalesced
    // bundles as a single datagram. Zero means "caller did not open a
    // datagram"; ToJson() will omit the field in that case.
    qlog_data.datagram_id = current_send_datagram_id_;

    // Pass the rich frame objects so the serializer can emit
    // per-frame fields (stream_id/offset/length, ack ranges, ...).
    auto& frames = packet->GetFrames();
    qlog_data.frame_objects.reserve(frames.size());
    for (const auto& frame : frames) {
        qlog_data.frame_objects.push_back(frame);
    }

    QLOG_PACKET_SENT(qlog_trace_, qlog_data);

    // Accumulate into the in-progress datagram so EndSendDatagram can
    // later emit a single transport:datagrams_sent event with the full
    // packet_numbers list and raw_length.
    if (current_send_datagram_id_ != 0) {
        current_send_packet_count_++;
        current_send_raw_length_ += pkt_len;
        current_send_packet_numbers_.push_back(packet->GetPacketNumber());
    }
}

std::function<void()> SendControl::MakeRetransmitTimeoutHandler(
    const std::shared_ptr<IPacket>& packet, uint32_t pkt_len, PacketNumberSpace ns) {
    return [this, pkt_len, packet, ns] {
        // NOTE: Do NOT call rtt_calculator_.OnPTOExpired() here.
        // PTO backoff is managed by the global OnPTOTimer() to avoid
        // exponential over-counting when multiple packets time out together
        // (e.g., packets 2-7 all lost → 6 callbacks → pto_count_ jumps to 6).

        // BUGFIX P0-1: Guard against double loss declaration.
        // If DetectLostPackets() already processed this packet (removed from
        // unacked_packets_ or marked is_lost), skip all operations to prevent
        // double-counting in congestion control (bytes_in_flight underflow).
        auto it = unacked_packets_[ns].find(packet->GetPacketNumber());
        if (it == unacked_packets_[ns].end() || it->second.is_lost) {
            return;  // Already handled by DetectLostPackets or a previous timer
        }

        it->second.is_lost = true;
        // Frame-level delivery: this packet is declared lost, notify any
        // tracked frames before the entry is (later) reaped.
        FireFrameDelivery(it->second, FrameDeliveryState::kLost);
        // Carry the stream_data along with the lost packet so that the
        // retransmitted PN re-inherits the same byte-range tracking when
        // the connection layer pushes it back through OnPacketSend.
        lost_packets_.push_back(LostPacketEntry{packet, it->second.stream_data});
        congestion_control_->OnPacketLost(LossEvent{packet->GetPacketNumber(), pkt_len, common::UTCTimeMsec() * 1000});

        // Metrics: Packet lost
        Metrics::CounterInc(common::MetricsStd::QuicPacketsLost);

        if (packet_lost_cb_) {
            packet_lost_cb_(packet);
        }
    };
}

void SendControl::TrackPacketForRetransmission(const std::shared_ptr<IPacket>& packet, uint32_t pkt_len,
    PacketNumberSpace ns, const std::vector<StreamDataInfo>& stream_data) {
    // Per-packet deadline is the *base* PTO, not the backed-off one.
    //
    // Exponential backoff is a property of the connection-level probe timer
    // (OnPTOTimer), which is what bounds a retransmission storm. Folding it
    // into this per-packet deadline as well meant every packet emitted after a
    // PTO inherited the already-inflated timeout: the first retransmission
    // waited 2x the base PTO, the next 4x, then 8x. Under sustained loss the
    // spacing grew faster than it could be recovered from, which is the
    // observed "1.5 s / 4.5 s" cadence -- sparse retransmissions that each
    // independently had only a small chance of getting through. Each packet
    // now gets a deadline measured from the current RTT, and the backoff that
    // prevents a storm still lives on the PTO timer.
    //
    // This stays on the conservative side: base PTO = SRTT + 4*RTTVAR is still
    // far longer than RFC 9002 §6.1.2's 9/8-RTT time threshold.
    //
    // While the handshake is unconfirmed the peer's advertised max_ack_delay
    // has not yet been reliably delivered and MUST be treated as 0 when
    // computing PTO; see GetEffectiveMaxAckDelay(). Once the teardown-path UAF
    // in ~SendControl (per-packet timer callbacks that captured `this`) was
    // fixed by delegating to ClearRetransmissionData(), it is safe to route
    // all four PTO callsites through the accessor.
    common::Timer retransmit_timer = scheduler_->AddTimer(life_token_, MakeRetransmitTimeoutHandler(packet, pkt_len, ns),
        static_cast<uint32_t>(rtt_calculator_.GetPTOInterval(GetEffectiveMaxAckDelay())));
    unacked_packets_[ns][packet->GetPacketNumber()] =
        PacketTimerInfo(largest_sent_time_[ns], pkt_len, std::move(retransmit_timer), stream_data, packet);
    LOG_DEBUG(
        "SendControl::OnPacketSend: saved packet %llu to unacked_packets[%d], stream_data count=%zu, "
        "unacked_packets[%d] size=%zu",
        packet->GetPacketNumber(), ns, stream_data.size(), ns, unacked_packets_[ns].size());

    // RFC 9002: Schedule PTO timer to detect persistent timeouts.
    // Rearm splices the existing node instead of freeing and reallocating one,
    // which matters because this runs once per outgoing packet.
    uint64_t pto_ms_send = rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay());
    ArmPtoTimer(pto_ms_send);
    LOG_DEBUG("SendControl::OnPacketSend: PTO armed, ns=%d pn=%llu pto_ms=%llu unacked[%d]_size=%zu", ns,
        packet->GetPacketNumber(), pto_ms_send, ns, unacked_packets_[ns].size());
}

void SendControl::OnPacketAck(uint64_t now, PacketNumberSpace ns, const std::shared_ptr<IFrame>& frame) {
    if (frame->GetType() != FrameType::kAck && frame->GetType() != FrameType::kAckEcn) {
        LOG_ERROR("invalid frame on packet ack.");
        return;
    }

    // PERF VALIDATION (recv-side queueing): time between consecutive ACK
    // frames being processed on the connection's worker thread.
    {
        static thread_local uint64_t last_ack_us = 0;
        uint64_t now_us = Metrics::NowUs();
        if (last_ack_us != 0 && now_us > last_ack_us) {
            Metrics::HistogramObserve(common::MetricsStd::DiagAckGapUs, now_us - last_ack_us);
        }
        last_ack_us = now_us;
    }

    // Count every ACK frame seen at the SendControl boundary.
    Metrics::CounterInc(common::MetricsStd::DiagAcksReceived);

    auto ack_frame = std::dynamic_pointer_cast<AckFrame>(frame);
    LOG_DEBUG("SendControl::OnPacketAck: largest_ack=%llu, first_ack_range=%u, ns=%d", ack_frame->GetLargestAck(),
        ack_frame->GetFirstAckRange(), ns);

    EmitQlogPacketsAcked(ack_frame);
    AckLargestAckedPacket(now, ns, frame, ack_frame);
    AckRangePackets(now, ns, ack_frame);

    // RFC 9002 Section 6.1: Detect lost packets based on packet/time threshold
    DetectLostPackets(now, ns, ack_frame->GetLargestAck());

    UpdatePtoAfterAck(ns);

    // Log recovery metrics with sampling
    LogRecoveryMetricsIfChanged(now);

    LOG_DEBUG("SendControl::OnPacketAck: completed for ns=%d, unacked_packets[%d] size=%zu", ns, ns,
        unacked_packets_[ns].size());
}

void SendControl::EmitQlogPacketsAcked(const std::shared_ptr<AckFrame>& ack_frame) {
    // Log packets_acked event to qlog
    if (!qlog_trace_) {
        return;
    }

    common::PacketsAckedData data;

    // Build ACK ranges
    uint64_t largest = ack_frame->GetLargestAck();
    uint64_t first_range = ack_frame->GetFirstAckRange();

    // First ACK range
    common::PacketsAckedData::AckRange range;
    range.start = largest - first_range;
    range.end = largest;
    data.ack_ranges.push_back(range);

    // Additional ACK ranges (qlog reporting). Per RFC 9000 §19.3.1, the
    // largest PN of the next range = first_range_smallest - gap_value - 2.
    // BUGFIX (G2 / Bug #22): the old "current_pkt -= gap" used 1-too-few
    // skips (matching the symmetric encoder/decoder bug elsewhere). After
    // that bug was fixed in the live ACK path, the qlog reporting path
    // must also be corrected so the recorded ranges reflect the actual
    // PNs that were just acknowledged.
    auto additional_ranges = ack_frame->GetAckRange();
    uint64_t current_pkt = largest - first_range;  // smallest PN of first range
    for (const auto& ack_range : additional_ranges) {
        current_pkt = current_pkt - ack_range.GetGap() - 2;  // largest PN of next range
        range.end = current_pkt;
        range.start = current_pkt - ack_range.GetAckRangeLength();
        data.ack_ranges.push_back(range);
        current_pkt = range.start;  // anchor for next iteration
    }

    // ACK delay (scale to microseconds)
    uint64_t scaled_ack_delay_ms = ack_frame->GetAckDelay() << ack_delay_exponent_;
    data.ack_delay_us = scaled_ack_delay_ms * 1000;  // Convert ms to us

    auto event_data = std::make_unique<common::PacketsAckedData>(data);
    QLOG_EVENT(qlog_trace_, common::QlogEvents::kPacketsAcked, std::move(event_data));
}

void SendControl::UpdateRttOnAck(
    uint64_t pkt_num, uint64_t send_time, uint64_t now, uint64_t scaled_ack_delay, size_t ack_range_count) {
    // Update RTT estimate with this ACK
    if (!rtt_calculator_.UpdateRtt(send_time, now, scaled_ack_delay)) {
        LOG_WARN("Failed to update RTT for packet %llu", pkt_num);
        return;
    }

    // Metrics: RTT updated
    Metrics::GaugeSet(common::MetricsStd::RttSmoothedUs, rtt_calculator_.GetSmoothedRtt() * 1000);
    Metrics::GaugeSet(common::MetricsStd::RttVarianceUs, rtt_calculator_.GetRttVar() * 1000);
    Metrics::GaugeSet(common::MetricsStd::RttMinUs, rtt_calculator_.GetMinRtt() * 1000);

    // Metrics: ACK delay
    Metrics::GaugeSet(common::MetricsStd::AckDelayUs, scaled_ack_delay);

    // Metrics: ACK ranges per frame
    Metrics::GaugeSet(common::MetricsStd::AckRangesPerFrame, ack_range_count);
}

bool SendControl::ValidateEcnCounters(PacketNumberSpace ns, const std::shared_ptr<IFrame>& frame) {
    auto ack_ecn = std::dynamic_pointer_cast<AckEcnFrame>(frame);
    if (!ack_ecn) {
        return false;
    }

    // Validate ECN counters are non-decreasing per RFC (§13.4 of RFC9000)
    uint64_t ect0 = ack_ecn->GetEct0();
    uint64_t ect1 = ack_ecn->GetEct1();
    uint64_t ce = ack_ecn->GetEcnCe();
    auto& prev_ect0 = prev_ect0_[ns];
    auto& prev_ect1 = prev_ect1_[ns];
    auto& prev_ce = prev_ce_[ns];
    auto& state = ecn_state_[ns];
    if (state == EcnState::kUnknown) {
        state = EcnState::kValidated;  // optimistic start
    }
    if (ect0 < prev_ect0 || ect1 < prev_ect1 || ce < prev_ce) {
        state = EcnState::kFailed;  // disable ECN responses if invalid
        return false;
    }
    prev_ect0 = ect0;
    prev_ect1 = ect1;
    prev_ce = ce;
    return ce > 0;
}

SendControl::AckOneResult SendControl::AckOnePacket(PacketNumberSpace ns, uint64_t pkt_num, uint64_t now,
    uint64_t ack_delay, bool ecn_ce, StreamAckLogLevel stream_log) {
    auto task = unacked_packets_[ns].find(pkt_num);
    if (task == unacked_packets_[ns].end()) {
        return AckOneResult::kNotFound;
    }

    if (stream_log == StreamAckLogLevel::kPerStreamWithMissLog) {
        LOG_DEBUG("SendControl::OnPacketAck: found packet %llu, stream_data count=%zu", pkt_num,
            task->second.stream_data.size());
    }

    bool was_lost = task->second.is_lost;

    // Cancel the packet's retransmit timer: it is now ACKed.
    task->second.timer_.Cancel();

    // Only notify congestion control if packet wasn't already declared lost
    // (the loss path already decremented bytes_in_flight).
    if (!was_lost) {
        congestion_control_->OnPacketAcked(
            AckEvent{pkt_num, task->second.pkt_len_, now * 1000, ack_delay, ecn_ce, task->second.send_time_ * 1000});

        // Metrics: Packet acknowledged
        Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
    }

    // Notify stream data ACK if callback is set
    if (stream_data_ack_cb_ && !task->second.stream_data.empty()) {
        if (stream_log != StreamAckLogLevel::kQuiet) {
            LOG_DEBUG("SendControl::OnPacketAck: notifying %zu streams for packet %llu",
                task->second.stream_data.size(), pkt_num);
        }
        for (const auto& stream_info : task->second.stream_data) {
            if (stream_log != StreamAckLogLevel::kQuiet) {
                LOG_DEBUG("SendControl::OnPacketAck: calling callback for stream_id=%llu, offset=%llu, len=%llu, "
                          "has_fin=%d",
                    stream_info.stream_id, stream_info.offset_start, stream_info.length, stream_info.has_fin);
            }
            stream_data_ack_cb_(
                stream_info.stream_id, stream_info.offset_start, stream_info.length, stream_info.has_fin);
        }
    } else if (stream_log == StreamAckLogLevel::kPerStreamWithMissLog) {
        LOG_DEBUG("SendControl::OnPacketAck: callback=%d, stream_data.empty()=%d", stream_data_ack_cb_ ? 1 : 0,
            task->second.stream_data.empty());
    }

    // Frame-level delivery: acknowledged, notify tracked frames.
    FireFrameDelivery(task->second, FrameDeliveryState::kAcked);

    // Remove from unacked_packets now that it's ACKed
    unacked_packets_[ns].erase(task);

    return was_lost ? AckOneResult::kAlreadyLost : AckOneResult::kAckedLive;
}

void SendControl::AckLargestAckedPacket(
    uint64_t now, PacketNumberSpace ns, const std::shared_ptr<IFrame>& frame, const std::shared_ptr<AckFrame>& ack_frame) {
    uint64_t pkt_num = ack_frame->GetLargestAck();
    if (pkt_num_largest_acked_[ns] >= pkt_num) {
        return;
    }
    pkt_num_largest_acked_[ns] = pkt_num;

    auto iter = unacked_packets_[ns].find(pkt_num);
    if (iter == unacked_packets_[ns].end()) {
        return;
    }

    // Scale peer-reported ACK delay by exponent to milliseconds
    uint64_t scaled_ack_delay = ack_frame->GetAckDelay() << ack_delay_exponent_;
    // Metrics: ACK ranges per frame (first range + additional ranges)
    size_t ack_range_count = 1 + ack_frame->GetAckRange().size();
    UpdateRttOnAck(pkt_num, iter->second.send_time_, now, scaled_ack_delay, ack_range_count);

    // RFC 9000 §13.4 ECN validation; only an AckEcnFrame carries counters.
    bool ecn_ce = frame->GetType() == FrameType::kAckEcn && ValidateEcnCounters(ns, frame);

    // Common epilogue: timer cancel, CC ack, stream callbacks, frame
    // delivery, table removal. The CC round-trip sample below runs after the
    // epilogue returns; it only reads rtt_calculator_ / congestion_control_,
    // which the stream and frame callbacks do not touch, so the ordering
    // shift relative to the pre-refactor code is unobservable.
    if (AckOnePacket(ns, pkt_num, now, ack_frame->GetAckDelay(), ecn_ce, StreamAckLogLevel::kPerStream) ==
        AckOneResult::kAckedLive) {
        // BUGFIX: RttCalculator reports in milliseconds, but CC algorithms
        // (BBR v1/v2/v3) store srtt_us_/min_rtt_us_ in *microseconds*.
        // Without this ×1000 conversion, BBR's BDP = bw × min_rtt_us / 1e6
        // collapses to near-zero on loopback (min_rtt=1ms → treated as 1us),
        // permanently cwnd-blocking the sender.
        // Use max(1, ...) to guarantee at least 1ms (1000us) — on loopback
        // the ms-granularity clock often yields 0ms RTT samples.
        uint64_t srtt_us_for_cc = std::max<uint64_t>(1, rtt_calculator_.GetSmoothedRtt()) * 1000;
        congestion_control_->OnRoundTripSample(
            srtt_us_for_cc, static_cast<uint64_t>(ack_frame->GetAckDelay()) * 1000);

        // Metrics: Packet acknowledged (ACK aggregation ratio = QuicPacketsAcked / DiagAcksReceived).
        // Second increment on top of AckOnePacket's — intentional, see ratio above.
        Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
    }
}

void SendControl::AckRangePackets(uint64_t now, PacketNumberSpace ns, const std::shared_ptr<AckFrame>& ack_frame) {
    uint64_t pkt_num = ack_frame->GetLargestAck();

    // Process first ACK range and notify streams
    // NOTE: largest_ack (pkt_num) was already processed above for RTT, so skip it
    // by starting from pkt_num-1
    for (uint32_t i = 0; i < ack_frame->GetFirstAckRange(); i++) {
        pkt_num--;  // Move to next packet in range
        if (AckOnePacket(ns, pkt_num, now, ack_frame->GetAckDelay(), false, StreamAckLogLevel::kPerStreamWithMissLog) ==
            AckOneResult::kNotFound) {
            LOG_DEBUG("SendControl::OnPacketAck: packet %llu not found in unacked_packets", pkt_num);
        }
    }

    // Process additional ACK ranges
    auto ranges = ack_frame->GetAckRange();
    for (auto iter = ranges.begin(); iter != ranges.end(); iter++) {
        // RFC 9000 §19.3.1: each Gap field is encoded as one less than the
        // actual number of unacknowledged packets between the previous range
        // and this one. The largest PN of the next range is therefore:
        //     prev_smallest - (gap_value + 1) - 1
        //   = prev_smallest - gap_value - 2
        // BUGFIX (G2 / Bug #22): the previous code subtracted only
        // (gap_value + 1), landing one PN too high. With gap_value=0 this
        // ACKed the immediately-preceding PN (which is supposed to be in the
        // gap) instead of the actual range start, leaving a real-data PN
        // orphaned in unacked_packets_ until packet-threshold or PTO declared
        // it lost. That stalled SendStream byte-range bookkeeping (FIN was
        // never recognised as ACKed) and produced the cwnd-stuck-at-1..31B
        // fingerprint observed in interop transfer-loss runs.
        pkt_num = pkt_num - iter->GetGap() - 2;
        for (uint32_t i = 0; i <= iter->GetAckRangeLength(); i++) {
            AckOnePacket(ns, pkt_num, now, ack_frame->GetAckDelay(), false, StreamAckLogLevel::kQuiet);
            // BUGFIX P2-1: Only decrement pkt_num within the range, not after the last packet.
            // The extra decrement caused off-by-one for subsequent additional ranges:
            // next range would start at (lowest-1) instead of lowest.
            if (i < iter->GetAckRangeLength()) {
                pkt_num--;
            }
        }
    }
}

void SendControl::UpdatePtoAfterAck(PacketNumberSpace ns) {
    // RFC 9000 §4.1.2: an ACK that covers one of our 1-RTT packets proves the
    // peer holds our 1-RTT keys and got the handshake flight, so the handshake
    // is confirmed. That uncaps the PTO backoff (see
    // kMaxPTOBackoffUnconfirmed, quic/config.h) and ends the widened idle
    // timeout. Set before OnPacketAcked() below so both take effect on this
    // same ACK.
    if (ns == PacketNumberSpace::kApplicationNumberSpace) {
        rtt_calculator_.SetHandshakeConfirmed();
    }

    // RFC 9002: Reset PTO backoff on ACK (call once per ACK frame, not per packet)
    rtt_calculator_.OnPacketAcked();

    // Cancel PTO timer since we received an ACK; we'll re-arm below if needed.
    pto_timer_.Cancel();

    // RFC 9002 §6.2.1 (Bug #18 fix):
    //   "A sender SHOULD restart its PTO timer every time an ack-eliciting
    //    packet is sent or acknowledged ... The PTO timer MUST NOT be set if
    //    there are no ack-eliciting packets in flight."
    //
    // Previously we only re-armed during handshake (the !handshake_complete_
    // branch below).  After handshake completion, if the ACK we just processed
    // did NOT clear every in-flight ack-eliciting packet (e.g. a partial /
    // selective ACK that leaves an older retransmit still outstanding), the
    // PTO timer was permanently cancelled — and because OnPacketSend only
    // (re)arms PTO at the moment of transmission, the connection lost its
    // last-resort retransmission trigger.  Under high-loss `sim` runs this
    // manifested as: server retransmits pkt N, peer's ACK is dropped,
    // pacing/FC keeps stalling Send, no new packet is emitted, and PTO never
    // fires → 8s idle timeout (Bug #18, fixed).
    //
    // Correct behaviour: re-arm PTO whenever any ack-eliciting packet remains
    // in flight in any packet number space.  unacked_packets_[] only contains
    // ack-eliciting packets (see OnPacketSend's ACK-only early return), so
    // emptiness is a sufficient test.
    bool has_ack_eliciting_in_flight = false;
    for (int s = 0; s < PacketNumberSpace::kNumberSpaceCount; s++) {
        if (!unacked_packets_[s].empty()) {
            has_ack_eliciting_in_flight = true;
            break;
        }
    }

    if (has_ack_eliciting_in_flight) {
        // Re-arm with the freshly-reset backoff (OnPacketAcked above zeroed
        // pto_count_, so this is a non-backed-off PTO based on latest RTT).
        uint64_t pto_ms_ack = rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay());
        ArmPtoTimer(pto_ms_ack);
        LOG_DEBUG("SendControl::OnPacketAck: PTO re-armed (in-flight), pto_ms=%llu unacked[0/1/2]={%zu,%zu,%zu}",
            pto_ms_ack, unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size());
    } else if (!handshake_complete_) {
        // RFC 9002 §6.2.2.1: During handshake, keep PTO timer alive even when
        // there is no ack-eliciting data in flight, so the client sends PING
        // probes if the handshake stalls (e.g. server anti-amplification
        // limited).  probe_needed_cb_ is the PING-injection path.
        // RFC 9002 §6.2.1: pre-handshake path → GetEffectiveMaxAckDelay() returns 0.
        uint64_t pto_ms_hs = rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay());
        ArmPtoTimer(pto_ms_hs);
        LOG_DEBUG("SendControl::OnPacketAck: PTO armed (pre-handshake), pto_ms=%llu", pto_ms_hs);
    } else {
        // handshake done AND nothing ack-eliciting in flight → PTO not
        // needed per RFC 9002 §6.2.1; leave it cancelled.
        LOG_DEBUG(
            "SendControl::OnPacketAck: PTO LEFT CANCELLED (handshake done, nothing in-flight) "
            "unacked[0/1/2]={%zu,%zu,%zu}",
            unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size());
    }
}

void SendControl::CanSend(uint64_t now, uint64_t& can_send_bytes) {
    congestion_control_->CanSend(now, can_send_bytes);
}

void SendControl::UpdateConfig(const TransportParam& tp) {
    max_ack_delay_ = static_cast<uint32_t>(tp.GetMaxAckDelay());
    // Defence in depth: TransportParam::Decode already rejects values above 20 per
    // RFC 9000 §18.2, but this value drives `<<` below, where anything >= 64 is
    // undefined behaviour. Clamp so a future decode path that misses the check
    // cannot turn a peer-supplied value into UB.
    ack_delay_exponent_ =
        static_cast<uint32_t>(std::min<uint64_t>(tp.GetackDelayExponent(), TransportParam::kMaxAckDelayExponent));
}

void SendControl::ClearRetransmissionData() {
    LOG_DEBUG("SendControl::ClearRetransmissionData: clearing, unacked[0/1/2]={%zu,%zu,%zu}",
        unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size());
    lost_packets_.clear();
    for (int i = 0; i < PacketNumberSpace::kNumberSpaceCount; i++) {
        for (auto& pair : unacked_packets_[i]) {
            pair.second.timer_.Cancel();
        }
        unacked_packets_[i].clear();
    }
}

void SendControl::FireFrameDelivery(PacketTimerInfo& info, FrameDeliveryState state) {
    // The tracked packet keeps its frames (IPacket::GetFrames), and each frame
    // keeps its optional handler, so re-queued / retransmitted copies of the
    // same IPacket stay tracked for free. Handlers are reset-style and
    // therefore safe to fire repeatedly (PTO can re-queue an already-lost
    // packet; the duplicate kLost is absorbed by the handler's idempotence).
    if (!info.packet) {
        return;
    }
    for (auto& frame : info.packet->GetFrames()) {
        if (frame) {
            frame->NotifyDelivery(state);
        }
    }
}

// RFC 9000 Section 4.10: Discard packet number space state
void SendControl::RemoveStaleUnackedEntry(
    PacketNumberSpace ns, uint64_t stale_pn, const std::shared_ptr<IPacket>& packet) {
    auto it = unacked_packets_[ns].find(stale_pn);
    if (it == unacked_packets_[ns].end()) {
        // Nothing to do: DetectLostPackets() already erased it when it declared
        // the packet lost.
        return;
    }
    if (!it->second.packet || it->second.packet.get() != packet.get()) {
        LOG_WARN("SendControl: unacked entry pn=%llu in ns=%d belongs to a different packet, leaving it alone",
            (unsigned long long)stale_pn, ns);
        return;
    }
    // ~PacketTimerInfo cancels the armed per-packet timer through its handle.
    unacked_packets_[ns].erase(it);
    LOG_DEBUG("SendControl: dropped stale unacked entry pn=%llu in ns=%d before renumbering for retransmission",
        (unsigned long long)stale_pn, ns);
}

void SendControl::DiscardPacketNumberSpace(PacketNumberSpace ns) {
    // RFC 9002 §7 / Appendix A.10 (OnPacketNumberSpaceDiscarded): the unacked
    // bytes in a discarded space must leave bytes_in_flight — without this,
    // orphaned Initial/Handshake bytes permanently shrink the effective cwnd
    // for Application-level sends. They were NOT lost: calling OnPacketLost
    // here collapses the cwnd (integration regression 2026-09-01), so use the
    // dedicated discard accounting instead.
    uint64_t discarded_bytes = 0;
    for (auto& pair : unacked_packets_[ns]) {
        pair.second.timer_.Cancel();
        if (!pair.second.is_lost) {
            discarded_bytes += pair.second.pkt_len_;
        }
    }
    if (discarded_bytes > 0) {
        congestion_control_->OnPacketsDiscarded(discarded_bytes);
    }
    unacked_packets_[ns].clear();

    // Remove any lost packets from this space
    for (auto it = lost_packets_.begin(); it != lost_packets_.end();) {
        if (it->packet && CryptoLevel2PacketNumberSpace(it->packet->GetCryptoLevel()) == ns) {
            it = lost_packets_.erase(it);
        } else {
            ++it;
        }
    }
    pkt_num_largest_sent_[ns] = 0;
    pkt_num_largest_acked_[ns] = 0;
    largest_sent_time_[ns] = 0;

    LOG_INFO("SendControl: Discarded packet number space %d per RFC 9000", ns);
}

// Reset Initial packet state for Retry (clear unacked/lost packets, keep PN tracking)
void SendControl::ResetInitialPacketNumber() {
    PacketNumberSpace ns = PacketNumberSpace::kInitialNumberSpace;

    // Clear unacked packets for Initial space
    for (auto& pair : unacked_packets_[ns]) {
        pair.second.timer_.Cancel();
    }
    unacked_packets_[ns].clear();

    // Remove any lost packets from Initial space
    for (auto it = lost_packets_.begin(); it != lost_packets_.end();) {
        if (it->packet && CryptoLevel2PacketNumberSpace(it->packet->GetCryptoLevel()) == ns) {
            it = lost_packets_.erase(it);
        } else {
            ++it;
        }
    }

    // Do NOT reset pkt_num_largest_sent_ / pkt_num_largest_acked_ / largest_sent_time_
    // The PN counter must continue incrementing after Retry per interop requirements

    LOG_INFO("SendControl: Reset Initial packet state for Retry (PN not reset)");
}

// RFC 9002 Section 6.1: Detect lost packets based on packet/time threshold
void SendControl::DetectLostPackets(uint64_t now, PacketNumberSpace ns, uint64_t largest_acked) {
    // RFC 9002 Section 6.1.2: Time threshold = 9/8 * smoothed_RTT
    uint64_t loss_delay = (rtt_calculator_.GetSmoothedRtt() * kTimeThresholdNum) / kTimeThresholdDen;
    loss_delay = std::max(loss_delay, uint64_t(1));  // At least 1ms

    // Find send time of largest_acked packet for time threshold calculation
    uint64_t largest_acked_send_time = 0;
    auto largest_iter = unacked_packets_[ns].find(largest_acked);
    if (largest_iter != unacked_packets_[ns].end()) {
        largest_acked_send_time = largest_iter->second.send_time_;
    }

    // Check all unacked packets with pkt_num < largest_acked
    std::vector<uint64_t> lost_packet_nums;
    for (auto& pair : unacked_packets_[ns]) {
        uint64_t pkt_num = pair.first;
        auto& info = pair.second;

        if (pkt_num >= largest_acked) continue;  // Skip packets >= largest_acked
        if (info.is_lost) continue;              // Already marked as lost

        bool should_declare_lost = false;

        // RFC 9002 Section 6.1.1: Packet threshold
        // Declare lost if kPacketThreshold (3) packets with higher numbers are acknowledged
        if (largest_acked >= pkt_num + kPacketThreshold) {
            should_declare_lost = true;
            LOG_DEBUG("DetectLostPackets: packet %llu lost by packet threshold (largest_acked=%llu, threshold=%u)",
                pkt_num, largest_acked, kPacketThreshold);
        }

        // RFC 9002 Section 6.1.2: Time threshold
        // Declare lost if sent more than loss_delay before largest_acked
        if (!should_declare_lost && largest_acked_send_time > 0) {
            uint64_t time_since_sent =
                (largest_acked_send_time > info.send_time_) ? (largest_acked_send_time - info.send_time_) : 0;
            if (time_since_sent > loss_delay) {
                should_declare_lost = true;
                LOG_DEBUG(
                    "DetectLostPackets: packet %llu lost by time threshold (time_since_sent=%llums, loss_delay=%llums)",
                    pkt_num, time_since_sent, loss_delay);
            }
        }

        if (should_declare_lost) {
            lost_packet_nums.push_back(pkt_num);
        }
    }

    // Mark packets as lost and trigger retransmission
    for (uint64_t pkt_num : lost_packet_nums) {
        auto it = unacked_packets_[ns].find(pkt_num);
        if (it != unacked_packets_[ns].end()) {
            it->second.timer_.Cancel();  // Cancel PTO timer

            // Add to lost_packets_ list for retransmission, carrying the
            // original packet's stream_data so the retransmitted PN can
            // re-register the same byte-range tracking with SendStream.
            if (it->second.packet) {
                // Frame-level delivery: declared lost by threshold, notify
                // tracked frames so their producers can re-emit fresh copies.
                FireFrameDelivery(it->second, FrameDeliveryState::kLost);
                lost_packets_.push_back(LostPacketEntry{it->second.packet, it->second.stream_data});
            }

            // Notify congestion control
            congestion_control_->OnPacketLost(LossEvent{pkt_num, it->second.pkt_len_, now * 1000});

            // Metrics: Packet lost
            Metrics::CounterInc(common::MetricsStd::QuicPacketsLost);

            // Trigger retransmission callback
            if (packet_lost_cb_ && it->second.packet) {
                packet_lost_cb_(it->second.packet);
            }

            LOG_WARN(
                "DetectLostPackets: declared packet %llu lost, triggering retransmission sc=%p", pkt_num, (void*)this);

            // Log marked_for_retransmit event
            if (qlog_trace_) {
                common::MarkedForRetransmitData retransmit_data;
                retransmit_data.packet_number = pkt_num;
                retransmit_data.trigger = "loss_detected";
                QLOG_MARKED_FOR_RETRANSMIT(qlog_trace_, retransmit_data);
            }

            // Log packet_lost event to qlog
            if (qlog_trace_ && it->second.packet) {
                common::PacketLostData data;
                data.packet_number = pkt_num;
                data.packet_type = it->second.packet->GetHeader()->GetPacketType();

                // Determine trigger reason by re-checking conditions
                if (largest_acked >= pkt_num + kPacketThreshold) {
                    data.trigger = "packet_threshold";
                } else if (largest_acked_send_time > 0 &&
                           (largest_acked_send_time - it->second.send_time_) > loss_delay) {
                    data.trigger = "time_threshold";
                } else {
                    data.trigger = "pto_expired";  // Fallback
                }

                QLOG_PACKET_LOST(qlog_trace_, data);
            }

            // BUGFIX: Remove the lost packet from unacked_packets to prevent memory leak
            // The retransmitted packet will be added with a new packet number
            unacked_packets_[ns].erase(it);
        }
    }

    if (!lost_packet_nums.empty()) {
        LOG_INFO("DetectLostPackets: detected %zu lost packets in ns=%d", lost_packet_nums.size(), ns);
    }

    // Log recovery metrics with sampling
    LogRecoveryMetricsIfChanged(now);
}

// RFC 9002 §6.2: PTO timer callback - called when PTO expires without receiving ACK
void SendControl::OnPTOTimer() {
    // RFC 9002: Increment PTO backoff once per PTO firing (not per packet)
    rtt_calculator_.OnPTOExpired();

    LOG_WARN("SendControl::OnPTOTimer: PTO fired, pto_count=%u, triggering probe sc=%p",
        rtt_calculator_.GetConsecutivePTOCount(), (void*)this);
    LOG_DEBUG("SendControl::OnPTOTimer: entry, unacked[0/1/2]={%zu,%zu,%zu} handshake_complete=%d",
        unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size(),
        handshake_complete_ ? 1 : 0);

    // RFC 9002 §6.2.4: Send probe packets to elicit ACK from peer
    // Trigger retransmission via the packet_lost_cb_ chain → ActiveSend → TrySend
    bool found_retransmit = false;
    if (packet_lost_cb_) {
        // Find the oldest unacked packet to probe with
        // This ensures a probe is sent even if per-packet timers haven't fired yet
        for (int ns = 0; ns < PacketNumberSpace::kNumberSpaceCount; ns++) {
            // When the peer has sent undecryptable 1-RTT, its Finished was
            // lost.  Skip Initial packets so the PTO probe targets Handshake
            // level, which elicits an ACK exposing the missing Finished.
            // EXCEPTION: never skip while an Initial packet carrying CRYPTO
            // (our ServerHello) is still unacked — a PSK/0-RTT client can
            // emit 1-RTT packets without having received any of our flight,
            // so the inference is void until the ServerHello is delivered
            // (see TrySendRetransmit for the full story).
            if (ns == PacketNumberSpace::kInitialNumberSpace && skip_initial_for_pto_) {
                bool has_unacked_crypto = false;
                for (const auto& entry : unacked_packets_[ns]) {
                    if (entry.second.packet && (entry.second.packet->GetFrameTypeBit() & FrameTypeBit::kCryptoBit)) {
                        has_unacked_crypto = true;
                        break;
                    }
                }
                if (!has_unacked_crypto) {
                    continue;
                }
            }
            auto& unacked = unacked_packets_[ns];
            if (unacked.empty()) {
                continue;
            }

            // Zombie-entry fix. Entries declared lost by the per-packet
            // retransmit timer (or by an earlier PTO) stay in unacked_packets_
            // until an ACK or DetectLostPackets() removes them (see the
            // "lost_packets_.empty()" note below). Probing only begin()
            // therefore gets stuck on the oldest zombie forever: once it is
            // marked is_lost, every later PTO leaves found_retransmit false,
            // no retransmission is queued, and the connection stalls until the
            // peer's idle timeout -- the handshakecorruption failure against
            // s2n-quic/quinn, where the 1024-byte response was sent once, lost
            // to the 30% corruption, and never probed again. Walk past the
            // zombies and probe the oldest packet not yet declared lost.
            // Drop entries whose packet is already gone. They can never be
            // probed, yet they keep unacked_packets_ non-empty -- which both
            // blocks the scan below and cancels the PTO timer -- so the
            // connection goes silent until the peer's idle timeout.
            for (auto cur = unacked.begin(); cur != unacked.end();) {
                if (!cur->second.packet) {
                    LOG_WARN("SendControl::OnPTOTimer: dropping unprobeable entry ns=%d pn=%llu (null packet)", ns,
                        (unsigned long long)cur->first);
                    cur->second.timer_.Cancel();
                    cur = unacked.erase(cur);
                } else {
                    ++cur;
                }
            }
            if (unacked.empty()) {
                continue;
            }

            auto it = unacked.end();
            for (auto cur = unacked.begin(); cur != unacked.end(); ++cur) {
                if (!cur->second.is_lost) {
                    it = cur;
                    break;
                }
            }

            if (it == unacked.end()) {
                // Every entry in this space is already declared lost: the
                // retransmissions were emitted and lost again ("retx of
                // retx"). Re-queue the oldest one so this PTO cycle still
                // carries something ack-eliciting instead of stalling. It is
                // already accounted as lost, so do not report it to
                // congestion control a second time.
                //
                // Re-queueing even when it is already sitting in lost_packets_
                // is deliberate: a queued retransmission can sit unsent for a
                // long time (cwnd / flow-control headroom), and PTO is the
                // deadline that must still produce an ack-eliciting packet
                // (RFC 9002 §6.2.4). The PTO backoff bounds the extra copies.
                auto oldest = unacked.begin();
                LOG_WARN("SendControl::OnPTOTimer: ns=%d all %zu entries already lost, re-queueing pn=%llu", ns,
                    unacked.size(), (unsigned long long)oldest->first);
                // Frame-level delivery: a duplicate kLost on an entry whose
                // frames were already notified is absorbed by the handlers'
                // reset-style idempotence.
                FireFrameDelivery(oldest->second, FrameDeliveryState::kLost);
                lost_packets_.push_back(LostPacketEntry{oldest->second.packet, oldest->second.stream_data});
                packet_lost_cb_(oldest->second.packet);
                found_retransmit = true;
                break;
            }

            // Mark as lost and trigger retransmission
            it->second.is_lost = true;
            // Frame-level delivery: PTO-declared loss, notify tracked frames.
            FireFrameDelivery(it->second, FrameDeliveryState::kLost);
            lost_packets_.push_back(LostPacketEntry{it->second.packet, it->second.stream_data});
            congestion_control_->OnPacketLost(LossEvent{it->first, it->second.pkt_len_, common::UTCTimeMsec() * 1000});
            it->second.timer_.Cancel();

            // Log marked_for_retransmit event (PTO-triggered)
            if (qlog_trace_) {
                common::MarkedForRetransmitData retransmit_data;
                retransmit_data.packet_number = it->first;
                retransmit_data.trigger = "pto_expired";
                QLOG_MARKED_FOR_RETRANSMIT(qlog_trace_, retransmit_data);
            }

            packet_lost_cb_(it->second.packet);
            found_retransmit = true;
            break;
        }
    }

    // RFC 9002 §6.2.2.1: During handshake, if no ACK-eliciting data to retransmit,
    // client MUST send a PING in Initial or Handshake space to elicit ACK from server
    //
    // lost_packets_.empty() guard (L1 "Expected 50. Got: 54"):
    // found_retransmit==false does NOT mean "nothing to retransmit". Within one
    // PTO window the per-packet retransmit timer is armed *before* the global
    // PTO timer (OnPacketSend: AddTimer then ArmPtoTimer), so it fires first,
    // marks the packet is_lost and pushes it into lost_packets_ -- but it does
    // not erase the unacked_packets_ entry. OnPTOTimer then runs, and its
    // begin() probe lands on exactly that already-is_lost entry, leaving
    // found_retransmit false even though a retransmission is queued.
    //
    // The result used to be a 1200-byte PING-only Initial emitted ~40us after
    // every PTO retransmission. Under handshakeloss (30% drop) that PING was
    // sometimes the *first* packet to reach the server -- with no CRYPTO in it,
    // msquic cannot build the connection and answers statelessly, echoing the
    // client's DCID as its SCID (20 bytes instead of its usual 9). The runner
    // counts unique server Initial SCIDs, so each such exchange inflated the
    // handshake count by one ("off-by-N").
    //
    // The point of the probe is to elicit an ACK when there is nothing
    // ack-eliciting left to send. If lost_packets_ is non-empty we already have
    // an ack-eliciting retransmission pending, so the PING is both redundant
    // and actively harmful.
    if (!found_retransmit && lost_packets_.empty() && !handshake_complete_ && probe_needed_cb_) {
        LOG_WARN("SendControl::OnPTOTimer: handshake not complete, no data to retransmit, sending probe");
        probe_needed_cb_();
    }

    // RFC 9002 §6.2.4 (Bug-19 fix): post-handshake probe with PING.
    // The retransmission path above re-emits the *same* original frames with a
    // new PN. If those same frames keep losing on the wire (high-loss links,
    // peer's RX buffer full because peer hasn't yielded MAX_DATA, etc.), the
    // peer never sees a packet that advances loss detection at our end and
    // the connection stalls.  RFC 9002 §6.2.4 explicitly says the probe MUST
    // be ack-eliciting; sending a PING (in addition to the retransmit) makes
    // sure that even if every retransmitted byte is dropped, a *fresh* tiny
    // packet still has its own chance to reach the peer and elicit an ACK,
    // which is what advances both packet-threshold loss detection and the
    // peer's flow-control update.
    //
    // We deliberately fire this even when found_retransmit==true: the cost is
    // a single 22-byte PING per PTO cycle, and it eliminates the
    // "retx-of-retx never reaches peer" failure mode observed in
    // transfer-5MB / quicx-quic-go interop (see PTO arm expire analysis).
    if (handshake_complete_ && application_probe_cb_) {
        LOG_WARN("SendControl::OnPTOTimer: post-handshake, scheduling PING probe (found_retransmit=%d)",
            found_retransmit ? 1 : 0);
        application_probe_cb_();
    }

    // Reschedule PTO timer with updated backoff for next probe.
    // RFC 9002 §6.2.1: route through GetEffectiveMaxAckDelay() so the pre-handshake
    // PTO treats peer max_ack_delay as 0 per spec.
    //
    // Note this rearms from inside the PTO callback itself. That is safe: the
    // node was parked before the callback ran, so Rearm simply re-links it.
    ArmPtoTimer(rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay()));
}

void SendControl::ArmPtoTimer(uint64_t delay_ms) {
    uint32_t delay = static_cast<uint32_t>(delay_ms);
    // Rearm keeps the same node and the same handle, so the per-packet path pays
    // no allocation and no re-copy of the callback. It fails only when we have no
    // timer yet (or it was cancelled outright), in which case we arm a new one.
    if (pto_timer_.Rearm(delay)) {
        return;
    }
    if (!scheduler_) {
        return;
    }
    pto_timer_ = scheduler_->AddTimer(life_token_, [this]() { OnPTOTimer(); }, delay);
}

void SendControl::SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) {
    qlog_trace_ = trace;
    if (congestion_control_) {
        congestion_control_->SetQlogTrace(trace);
    }
}

void SendControl::BeginSendDatagram(uint64_t datagram_id) {
    // No-op when qlog isn't enabled: the per-packet accumulator path inside
    // OnPacketSend already guards on qlog_trace_, so just keep the id at 0
    // and avoid touching the vector to stay branch-free in the no-qlog hot
    // path. When qlog is enabled we still want the id even if the caller
    // doesn't end up calling EndSendDatagram (e.g. SendImmediateAck only
    // emits one packet) — the id annotation on packet_sent is useful on
    // its own.
    current_send_datagram_id_ = datagram_id;
    current_send_packet_count_ = 0;
    current_send_raw_length_ = 0;
    current_send_packet_numbers_.clear();
}

SendControl::SendDatagramSummary SendControl::EndSendDatagram() {
    SendDatagramSummary out;
    out.datagram_id = current_send_datagram_id_;
    out.packet_count = current_send_packet_count_;
    out.raw_length = current_send_raw_length_;
    out.packet_numbers = std::move(current_send_packet_numbers_);
    // Reset accumulator for the next datagram.
    current_send_datagram_id_ = 0;
    current_send_packet_count_ = 0;
    current_send_raw_length_ = 0;
    current_send_packet_numbers_.clear();
    return out;
}

void SendControl::LogRecoveryMetricsIfChanged(uint64_t now) {
    if (!qlog_trace_ || !congestion_control_) {
        return;
    }

    uint64_t current_cwnd = congestion_control_->GetCongestionWindow();

    // Sampling strategy: CWND changed by more than 10% or time elapsed > 100ms
    bool significant_change = false;
    if (last_logged_cwnd_ > 0) {
        int64_t cwnd_diff = std::abs(static_cast<int64_t>(current_cwnd) - static_cast<int64_t>(last_logged_cwnd_));
        significant_change = (cwnd_diff * 10 > static_cast<int64_t>(last_logged_cwnd_));
    }

    bool time_elapsed =
        (last_metrics_log_time_ == 0) || ((now - last_metrics_log_time_) >= 100000);  // 100ms in microseconds

    if (!significant_change && !time_elapsed) {
        return;
    }

    // Update sampling state
    last_logged_cwnd_ = current_cwnd;
    last_metrics_log_time_ = now;

    // Log recovery metrics event
    common::RecoveryMetricsData data;
    data.min_rtt_us = rtt_calculator_.GetMinRtt() * 1000;
    data.smoothed_rtt_us = rtt_calculator_.GetSmoothedRtt() * 1000;
    data.latest_rtt_us = rtt_calculator_.GetLatestRtt() * 1000;
    data.rtt_variance_us = rtt_calculator_.GetRttVar() * 1000;
    data.cwnd_bytes = current_cwnd;
    data.bytes_in_flight = congestion_control_->GetBytesInFlight();
    data.ssthresh = congestion_control_->GetSsthresh();
    // qlog field is bits/sec; CC reports bytes/sec, so convert here.
    data.pacing_rate_bps = congestion_control_->GetPacingRateBytesPerSec() * 8ull;

    QLOG_METRICS_UPDATED(qlog_trace_, data);
}

}  // namespace quic
}  // namespace quicx
