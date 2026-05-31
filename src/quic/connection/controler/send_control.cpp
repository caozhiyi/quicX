#include <algorithm>
#include <cstring>
#include <string>

#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/qlog/qlog.h"

#include "quic/config.h"
#include "quic/congestion_control/congestion_control_factory.h"
#include "quic/connection/controler/send_control.h"
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"

namespace quicx {
namespace quic {

SendControl::SendControl(std::shared_ptr<common::ITimer> timer):
    timer_(timer),
    max_ack_delay_(kMaxAckDelay) {
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
    common::Metrics::CounterInc(common::MetricsStd::QuicPacketsTx);

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

    // Count this packet in congestion control (bytes_in_flight)
    // BUGFIX: CC algorithms (BBR/Cubic) use microsecond-based internal timing.
    // The system clock (UTCTimeMsec) provides milliseconds; multiply by 1000
    // so that CC bandwidth/BDP/epoch calculations use the correct time scale.
    congestion_control_->OnPacketSent(SentPacketEvent{packet->GetPacketNumber(), pkt_len, now * 1000, false});

    // Log packet_sent event to qlog
    if (qlog_trace_) {
        common::PacketSentData qlog_data;
        qlog_data.packet_number = packet->GetPacketNumber();
        qlog_data.packet_type = packet->GetHeader()->GetPacketType();
        qlog_data.packet_size = pkt_len;

        // Pass the rich frame objects so the serializer can emit
        // per-frame fields (stream_id/offset/length, ack ranges, ...).
        auto& frames = packet->GetFrames();
        qlog_data.frame_objects.reserve(frames.size());
        for (const auto& frame : frames) {
            qlog_data.frame_objects.push_back(frame);
        }

        QLOG_PACKET_SENT(qlog_trace_, qlog_data);
    }

    // Track when we last sent ack-eliciting data for PTO timer
    last_ack_eliciting_sent_time_ = now;

    auto timer_task = common::TimerTask([this, pkt_len, packet, ns] {
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
        // Carry the stream_data along with the lost packet so that the
        // retransmitted PN re-inherits the same byte-range tracking when
        // the connection layer pushes it back through OnPacketSend.
        lost_packets_.push_back(LostPacketEntry{packet, it->second.stream_data});
        congestion_control_->OnPacketLost(LossEvent{packet->GetPacketNumber(), pkt_len, common::UTCTimeMsec() * 1000});

        // Metrics: Packet lost
        common::Metrics::CounterInc(common::MetricsStd::QuicPacketsLost);

        if (packet_lost_cb_) {
            packet_lost_cb_(packet);
        }
    });
    // RFC 9002 §6.2.1: Use PTO with exponential backoff. While the handshake
    // is unconfirmed the peer's advertised max_ack_delay has not yet been
    // reliably delivered and MUST be treated as 0 when computing PTO; see
    // GetEffectiveMaxAckDelay(). Once the teardown-path UAF in ~SendControl
    // (per-packet timer_task lambdas that captured `this`) was fixed by
    // delegating to ClearRetransmissionData(), it is safe to route all four
    // PTO callsites through the accessor.
    timer_->AddTimer(timer_task, rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay()));
    unacked_packets_[ns][packet->GetPacketNumber()] =
        PacketTimerInfo(largest_sent_time_[ns], pkt_len, timer_task, stream_data, packet);
    LOG_DEBUG(
        "SendControl::OnPacketSend: saved packet %llu to unacked_packets[%d], stream_data count=%zu, "
        "unacked_packets[%d] size=%zu",
        packet->GetPacketNumber(), ns, stream_data.size(), ns, unacked_packets_[ns].size());

    // RFC 9002: Schedule PTO timer to detect persistent timeouts
    // Cancel existing timer and reschedule with current PTO value
    timer_->RemoveTimer(pto_timer_);
    pto_timer_.SetTimeoutCallback(std::bind(&SendControl::OnPTOTimer, this));
    uint64_t pto_ms_send = rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay());
    timer_->AddTimer(pto_timer_, pto_ms_send);
    LOG_DEBUG(
        "SendControl::OnPacketSend: PTO armed, ns=%d pn=%llu pto_ms=%llu unacked[%d]_size=%zu",
        ns, packet->GetPacketNumber(), pto_ms_send, ns, unacked_packets_[ns].size());
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
        uint64_t now_us = common::Metrics::NowUs();
        if (last_ack_us != 0 && now_us > last_ack_us) {
            common::Metrics::HistogramObserve(
                common::MetricsStd::DiagAckGapUs, now_us - last_ack_us);
        }
        last_ack_us = now_us;
    }

    // Count every ACK frame seen at the SendControl boundary.
    common::Metrics::CounterInc(common::MetricsStd::DiagAcksReceived);

    auto ack_frame = std::dynamic_pointer_cast<AckFrame>(frame);
    LOG_DEBUG("SendControl::OnPacketAck: largest_ack=%llu, first_ack_range=%u, ns=%d",
        ack_frame->GetLargestAck(), ack_frame->GetFirstAckRange(), ns);

    // Log packets_acked event to qlog
    if (qlog_trace_) {
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

    uint64_t pkt_num = ack_frame->GetLargestAck();
    if (pkt_num_largest_acked_[ns] < pkt_num) {
        pkt_num_largest_acked_[ns] = pkt_num;

        auto iter = unacked_packets_[ns].find(pkt_num);
        if (iter != unacked_packets_[ns].end()) {
            // Scale peer-reported ACK delay by exponent to milliseconds
            uint64_t scaled_ack_delay = ack_frame->GetAckDelay() << ack_delay_exponent_;
            // Update RTT estimate with this ACK
            if (!rtt_calculator_.UpdateRtt(iter->second.send_time_, now, scaled_ack_delay)) {
                LOG_WARN("Failed to update RTT for packet %llu", pkt_num);
            } else {
                // Metrics: RTT updated
                common::Metrics::GaugeSet(common::MetricsStd::RttSmoothedUs, rtt_calculator_.GetSmoothedRtt() * 1000);
                common::Metrics::GaugeSet(common::MetricsStd::RttVarianceUs, rtt_calculator_.GetRttVar() * 1000);
                common::Metrics::GaugeSet(common::MetricsStd::RttMinUs, rtt_calculator_.GetMinRtt() * 1000);

                // Metrics: ACK delay
                common::Metrics::GaugeSet(common::MetricsStd::AckDelayUs, scaled_ack_delay);

                // Metrics: ACK ranges per frame
                size_t ack_range_count = 1 + ack_frame->GetAckRange().size();  // first range + additional ranges
                common::Metrics::GaugeSet(common::MetricsStd::AckRangesPerFrame, ack_range_count);
            }

            bool ecn_ce = false;
            if (frame->GetType() == FrameType::kAckEcn) {
                auto ack_ecn = std::dynamic_pointer_cast<AckEcnFrame>(frame);
                if (ack_ecn) {
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
                    } else {
                        prev_ect0 = ect0;
                        prev_ect1 = ect1;
                        prev_ce = ce;
                        if (ce > 0) ecn_ce = true;
                    }
                }
            }

            // Cancel the PTO timer since packet is ACKed
            timer_->RemoveTimer(iter->second.timer_task_);

            // Only notify congestion control if packet wasn't already declared lost
            if (!iter->second.is_lost) {
                congestion_control_->OnPacketAcked(
                    AckEvent{pkt_num, iter->second.pkt_len_, now * 1000, ack_frame->GetAckDelay(), ecn_ce,
                        iter->second.send_time_ * 1000});
                // BUGFIX: RttCalculator reports in milliseconds, but CC algorithms
                // (BBR v1/v2/v3) store srtt_us_/min_rtt_us_ in *microseconds*.
                // Without this ×1000 conversion, BBR's BDP = bw × min_rtt_us / 1e6
                // collapses to near-zero on loopback (min_rtt=1ms → treated as 1us),
                // permanently cwnd-blocking the sender.
                // Use max(1, ...) to guarantee at least 1ms (1000us) — on loopback
                // the ms-granularity clock often yields 0ms RTT samples.
                uint64_t srtt_us_for_cc = std::max<uint64_t>(1, rtt_calculator_.GetSmoothedRtt()) * 1000;
                congestion_control_->OnRoundTripSample(srtt_us_for_cc,
                    static_cast<uint64_t>(ack_frame->GetAckDelay()) * 1000);

                // Metrics: Packet acknowledged
                common::Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
                // Metrics: Packet acknowledged (ACK aggregation ratio = QuicPacketsAcked / DiagAcksReceived)
                common::Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
            }

            // Notify stream data ACK if callback is set
            if (stream_data_ack_cb_ && !iter->second.stream_data.empty()) {
                LOG_DEBUG("SendControl::OnPacketAck: notifying %zu streams for packet %llu",
                    iter->second.stream_data.size(), pkt_num);
                for (const auto& stream_info : iter->second.stream_data) {
                    LOG_DEBUG(
                        "SendControl::OnPacketAck: calling callback for stream_id=%llu, offset=%llu, len=%llu, "
                        "has_fin=%d",
                        stream_info.stream_id, stream_info.offset_start, stream_info.length, stream_info.has_fin);
                    stream_data_ack_cb_(
                        stream_info.stream_id, stream_info.offset_start, stream_info.length, stream_info.has_fin);
                }
            }

            // Remove from unacked_packets now that it's ACKed
            unacked_packets_[ns].erase(iter);
        }
    }

    // Process first ACK range and notify streams
    // NOTE: largest_ack (pkt_num) was already processed above for RTT, so skip it
    // by starting from pkt_num-1
    for (uint32_t i = 0; i < ack_frame->GetFirstAckRange(); i++) {
        pkt_num--;  // Move to next packet in range
        auto task = unacked_packets_[ns].find(pkt_num);
        if (task != unacked_packets_[ns].end()) {
            LOG_DEBUG("SendControl::OnPacketAck: found packet %llu, stream_data count=%zu", pkt_num,
                task->second.stream_data.size());
            timer_->RemoveTimer(task->second.timer_task_);

            // Notify congestion control to decrement bytes_in_flight
            if (!task->second.is_lost) {
                congestion_control_->OnPacketAcked(
                    AckEvent{pkt_num, task->second.pkt_len_, now * 1000, ack_frame->GetAckDelay(), false,
                        task->second.send_time_ * 1000});

                // Metrics: Packet acknowledged
                common::Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
            }

            // Notify stream data ACK if callback is set
            if (stream_data_ack_cb_ && !task->second.stream_data.empty()) {
                LOG_DEBUG("SendControl::OnPacketAck: notifying %zu streams for packet %llu",
                    task->second.stream_data.size(), pkt_num);
                for (const auto& stream_info : task->second.stream_data) {
                    LOG_DEBUG(
                        "SendControl::OnPacketAck: calling callback for stream_id=%llu, offset=%llu, len=%llu, "
                        "has_fin=%d",
                        stream_info.stream_id, stream_info.offset_start, stream_info.length, stream_info.has_fin);
                    stream_data_ack_cb_(
                        stream_info.stream_id, stream_info.offset_start, stream_info.length, stream_info.has_fin);
                }
            } else {
                LOG_DEBUG("SendControl::OnPacketAck: callback=%d, stream_data.empty()=%d",
                    stream_data_ack_cb_ ? 1 : 0, task->second.stream_data.empty());
            }

            // Remove from unacked_packets
            unacked_packets_[ns].erase(task);
        } else {
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
            auto task = unacked_packets_[ns].find(pkt_num);
            if (task != unacked_packets_[ns].end()) {
                timer_->RemoveTimer(task->second.timer_task_);

                // Notify congestion control to decrement bytes_in_flight
                // BUG FIX: Was missing this call, causing bytes_in_flight to leak
                if (!task->second.is_lost) {
                    congestion_control_->OnPacketAcked(
                        AckEvent{pkt_num, task->second.pkt_len_, now * 1000, ack_frame->GetAckDelay(), false,
                            task->second.send_time_ * 1000});

                    // Metrics: Packet acknowledged
                    common::Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
                }

                // Notify stream data ACK if callback is set
                if (stream_data_ack_cb_ && !task->second.stream_data.empty()) {
                    for (const auto& stream_info : task->second.stream_data) {
                        stream_data_ack_cb_(stream_info.stream_id, stream_info.offset_start, stream_info.length,
                            stream_info.has_fin);
                    }
                }

                // Remove from unacked_packets
                unacked_packets_[ns].erase(task);
            }
            // BUGFIX P2-1: Only decrement pkt_num within the range, not after the last packet.
            // The extra decrement caused off-by-one for subsequent additional ranges:
            // next range would start at (lowest-1) instead of lowest.
            if (i < iter->GetAckRangeLength()) {
                pkt_num--;
            }
        }
    }

    // RFC 9002 Section 6.1: Detect lost packets based on packet/time threshold
    DetectLostPackets(now, ns, ack_frame->GetLargestAck());

    // RFC 9002: Reset PTO backoff on ACK (call once per ACK frame, not per packet)
    rtt_calculator_.OnPacketAcked();

    // Cancel PTO timer since we received an ACK; we'll re-arm below if needed.
    timer_->RemoveTimer(pto_timer_);

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
    // fires → 8s idle timeout (TODO Bug #18).
    //
    // Correct behaviour: re-arm PTO whenever any ack-eliciting packet remains
    // in flight in any packet number space.  unacked_packets_[] only contains
    // ack-eliciting packets (see OnPacketSend line ~48 where ACK-only packets
    // early-return before insertion), so emptiness is a sufficient test.
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
        pto_timer_.SetTimeoutCallback(std::bind(&SendControl::OnPTOTimer, this));
        uint64_t pto_ms_ack = rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay());
        timer_->AddTimer(pto_timer_, pto_ms_ack);
        LOG_DEBUG(
            "SendControl::OnPacketAck: PTO re-armed (in-flight), pto_ms=%llu unacked[0/1/2]={%zu,%zu,%zu}",
            pto_ms_ack, unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size());
    } else if (!handshake_complete_) {
        // RFC 9002 §6.2.2.1: During handshake, keep PTO timer alive even when
        // there is no ack-eliciting data in flight, so the client sends PING
        // probes if the handshake stalls (e.g. server anti-amplification
        // limited).  probe_needed_cb_ is the PING-injection path.
        pto_timer_.SetTimeoutCallback(std::bind(&SendControl::OnPTOTimer, this));
        // RFC 9002 §6.2.1: pre-handshake path → GetEffectiveMaxAckDelay() returns 0.
        uint64_t pto_ms_hs = rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay());
        timer_->AddTimer(pto_timer_, pto_ms_hs);
        LOG_DEBUG("SendControl::OnPacketAck: PTO armed (pre-handshake), pto_ms=%llu", pto_ms_hs);
    } else {
        LOG_DEBUG(
            "SendControl::OnPacketAck: PTO LEFT CANCELLED (handshake done, nothing in-flight) unacked[0/1/2]={%zu,%zu,%zu}",
            unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size());
    }
    // else: handshake done AND nothing ack-eliciting in flight → PTO not
    // needed per RFC 9002 §6.2.1; leave it cancelled.

    // Log recovery metrics with sampling
    LogRecoveryMetricsIfChanged(now);

    LOG_DEBUG("SendControl::OnPacketAck: completed for ns=%d, unacked_packets[%d] size=%zu", ns, ns,
        unacked_packets_[ns].size());
}

void SendControl::CanSend(uint64_t now, uint64_t& can_send_bytes) {
    congestion_control_->CanSend(now, can_send_bytes);
}

void SendControl::UpdateConfig(const TransportParam& tp) {
    max_ack_delay_ = static_cast<uint32_t>(tp.GetMaxAckDelay());
    ack_delay_exponent_ = static_cast<uint32_t>(tp.GetackDelayExponent());
}

void SendControl::ClearRetransmissionData() {
    LOG_DEBUG(
        "SendControl::ClearRetransmissionData: clearing, unacked[0/1/2]={%zu,%zu,%zu}",
        unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size());
    lost_packets_.clear();
    for (int i = 0; i < PacketNumberSpace::kNumberSpaceCount; i++) {
        for (auto& pair : unacked_packets_[i]) {
            timer_->RemoveTimer(pair.second.timer_task_);
        }
        unacked_packets_[i].clear();
    }
}

// RFC 9000 Section 4.10: Discard packet number space state
void SendControl::DiscardPacketNumberSpace(PacketNumberSpace ns) {
    // Clear unacked packets for this space
    for (auto& pair : unacked_packets_[ns]) {
        timer_->RemoveTimer(pair.second.timer_task_);
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
        timer_->RemoveTimer(pair.second.timer_task_);
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
            LOG_DEBUG(
                "DetectLostPackets: packet %llu lost by packet threshold (largest_acked=%llu, threshold=%u)", pkt_num,
                largest_acked, kPacketThreshold);
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
            timer_->RemoveTimer(it->second.timer_task_);  // Cancel PTO timer

            // Add to lost_packets_ list for retransmission, carrying the
            // original packet's stream_data so the retransmitted PN can
            // re-register the same byte-range tracking with SendStream.
            if (it->second.packet) {
                lost_packets_.push_back(LostPacketEntry{it->second.packet, it->second.stream_data});
            }

            // Notify congestion control
            congestion_control_->OnPacketLost(LossEvent{pkt_num, it->second.pkt_len_, now * 1000});

            // Metrics: Packet lost
            common::Metrics::CounterInc(common::MetricsStd::QuicPacketsLost);

            // Trigger retransmission callback
            if (packet_lost_cb_ && it->second.packet) {
                packet_lost_cb_(it->second.packet);
            }

            LOG_WARN("DetectLostPackets: declared packet %llu lost, triggering retransmission", pkt_num);

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

    LOG_WARN(
        "SendControl::OnPTOTimer: PTO fired, pto_count=%u, triggering probe", rtt_calculator_.GetConsecutivePTOCount());
    LOG_DEBUG(
        "SendControl::OnPTOTimer: entry, unacked[0/1/2]={%zu,%zu,%zu} handshake_complete=%d",
        unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size(),
        handshake_complete_ ? 1 : 0);

    // RFC 9002 §6.2.4: Send probe packets to elicit ACK from peer
    // Trigger retransmission via the packet_lost_cb_ chain → ActiveSend → TrySend
    bool found_retransmit = false;
    if (packet_lost_cb_) {
        // Find the oldest unacked packet to probe with
        // This ensures a probe is sent even if per-packet timers haven't fired yet
        for (int ns = 0; ns < PacketNumberSpace::kNumberSpaceCount; ns++) {
            if (!unacked_packets_[ns].empty()) {
                auto it = unacked_packets_[ns].begin();
                if (it->second.packet && !it->second.is_lost) {
                    // Mark as lost and trigger retransmission
                    it->second.is_lost = true;
                    lost_packets_.push_back(LostPacketEntry{it->second.packet, it->second.stream_data});
                    congestion_control_->OnPacketLost(LossEvent{it->first, it->second.pkt_len_, common::UTCTimeMsec() * 1000});
                    timer_->RemoveTimer(it->second.timer_task_);

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
        }
    }

    // RFC 9002 §6.2.2.1: During handshake, if no ACK-eliciting data to retransmit,
    // client MUST send a PING in Initial or Handshake space to elicit ACK from server
    if (!found_retransmit && !handshake_complete_ && probe_needed_cb_) {
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
        LOG_WARN(
            "SendControl::OnPTOTimer: post-handshake, scheduling PING probe (found_retransmit=%d)",
            found_retransmit ? 1 : 0);
        application_probe_cb_();
    }

    // Reschedule PTO timer with updated backoff for next probe
    timer_->RemoveTimer(pto_timer_);
    pto_timer_.SetTimeoutCallback(std::bind(&SendControl::OnPTOTimer, this));
    // RFC 9002 §6.2.1: route through GetEffectiveMaxAckDelay() so the pre-handshake
    // PTO treats peer max_ack_delay as 0 per spec.
    timer_->AddTimer(pto_timer_, rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay()));
}

void SendControl::SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) {
    qlog_trace_ = trace;
    if (congestion_control_) {
        congestion_control_->SetQlogTrace(trace);
    }
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
