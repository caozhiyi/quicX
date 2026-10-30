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
#include "quic/connection/error.h"
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"

namespace quicx {
namespace quic {

namespace {
// RFC 9002 §7.6.2: how many completed ACK rounds keep their send-time
// anchors. The persistent-congestion window spans >= 3 PTO intervals, which
// several ACK rounds typically cover; anchors older than that can no longer
// interact with a candidate window that still passes the duration check.
constexpr size_t kMaxAckAnchors = 8;
}  // namespace

SendControl::SendControl(std::shared_ptr<common::ITimerScheduler> scheduler):
    max_ack_delay_(kMaxAckDelay),
    scheduler_(scheduler) {
    memset(pkt_num_largest_sent_, 0, sizeof(pkt_num_largest_sent_));
    memset(pkt_num_largest_acked_, 0, sizeof(pkt_num_largest_acked_));
    memset(largest_sent_time_, 0, sizeof(largest_sent_time_));
    memset(largest_acked_pn_, 0, sizeof(largest_acked_pn_));
    memset(largest_acked_send_time_, 0, sizeof(largest_acked_send_time_));

    // CC algorithm selection: the compile-time default (kDefaultCongestionControl
    // in quic/config.h). Runtime override via SetCongestionControlType(),
    // called by the worker before any packet is sent.
    CongestionControlType cc_type = CongestionControlTypeFromString("");
    active_cc_type_ = cc_type;
    LOG_INFO("SendControl: using congestion control: %s", CongestionControlTypeToString(cc_type));
    congestion_control_ = CreateCongestionControl(cc_type);
}

void SendControl::SetCongestionControlType(CongestionControlType type) {
    // Rebuilding the CC object mid-connection would drop bytes_in_flight and
    // cwnd state, so the override is only allowed before the first packet is
    // sent. The worker calls this right after connection creation, which is
    // always before OnPacketSend.
    if (cc_locked_) {
        LOG_WARN("SendControl: ignoring congestion control override after packets were already sent");
        return;
    }
    LOG_INFO("SendControl: overriding congestion control to: %s", CongestionControlTypeToString(type));
    active_cc_type_ = type;
    congestion_control_ = CreateCongestionControl(type);
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
    largest_sent_time_[ns] = common::MonotonicTimeMsec();
    // The first send locks the congestion control algorithm: rebuilding the
    // CC object afterwards would drop bytes_in_flight / cwnd state.
    cc_locked_ = true;

    // P0-C3 (RFC 9000 §13.2.3): remember the Largest Acknowledged of any ACK
    // frame riding in this packet, so that when the peer acknowledges this
    // packet we can tell the receiver side its earlier ACK frame arrived
    // (ack-of-ack) and it can stop re-reporting those ranges. Runs for
    // ACK-only packets AND ack-eliciting packets carrying a piggybacked ACK
    // frame — both are confirmed by the same peer ACK. Pure bookkeeping: no
    // loss-detection or CC implications (ACK frames are never retransmitted,
    // RFC 9002 §7.4).
    for (const auto& frame : packet->GetFrames()) {
        if (frame->GetType() == FrameType::kAck || frame->GetType() == FrameType::kAckEcn) {
            auto ack = std::static_pointer_cast<AckFrame>(frame);
            ack_of_ack_[ns][packet->GetPacketNumber()] = ack->GetLargestAck();
            break;  // one ACK frame per packet by construction
        }
    }

    // RFC 9002: Only ACK-eliciting packets count towards congestion control
    // ACK-only packets should NOT be accounted in bytes_in_flight
    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        LOG_DEBUG("SendControl::OnPacketSend: packet %llu is ACK-only, not counting in congestion control",
            packet->GetPacketNumber());
        return;
    }

    // RFC 9002 A.5 OnPacketSent: "if (ack_eliciting):
    // time_of_last_ack_eliciting_packet[pn_space] = now()". Per-space: the
    // PTO of this space restarts (§6.2.1 SHOULD restart), other spaces are
    // untouched — this is what keeps a keep-alive PING in Application Data
    // from deferring the PTO of an unacknowledged Handshake flight.
    last_ack_eliciting_send_time_[ns] = now;

    TrackPacketInCongestionControl(now, packet, pkt_len);
    EmitQlogPacketSent(packet, pkt_len);
    TrackPacketForRetransmission(packet, pkt_len, ns, stream_data);
}

void SendControl::TrackPacketInCongestionControl(
    uint64_t now, const std::shared_ptr<IPacket>& packet, uint32_t pkt_len) {
    // Count this packet in congestion control (bytes_in_flight)
    // CC algorithms (BBR/Cubic) and the pacer run on microsecond timing.
    // Snapshot the real microsecond clock here — the same clock SendManager's
    // pacing scheduler compares against — instead of scaling the caller's
    // millisecond snapshot by 1000: that rounding bakes in up to 1 ms of skew
    // between the event time and the scheduler's clock, enough to distort
    // sub-millisecond pacing gaps. |now| (ms) remains authoritative for the
    // PTO bookkeeping below, and largest_sent_time_ keeps its own independent
    // ms snapshot (TrackPacketForRetransmission), so neither domain changes.
    congestion_control_->OnPacketSent(SentPacketEvent{packet->GetPacketNumber(), pkt_len, Metrics::NowUs(), false});

    // ACK-eliciting bytes are on the wire: the PTO cycle's purpose (RFC 9002
    // §6.2.4 — elicit an ACK) is served, cancel any deferred probe.
    // TrackPacketInCongestionControl only runs for ACK-eliciting packets, so
    // a lone ACK-only send does NOT clear it (an ACK-only packet elicits
    // nothing and the probe must still go out).
    pto_probe_pending_ = false;

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

    // Accumulate into the in-progress datagram so EndSendDatagram can
    // later emit a single transport:datagrams_sent event with the full
    // packet_numbers list and raw_length.
    if (current_send_datagram_id_ != 0) {
        current_send_packet_count_++;
        current_send_raw_length_ += pkt_len;
        current_send_packet_numbers_.push_back(packet->GetPacketNumber());
        // Stage: the emitter commits (Flush) or discards (Drop) once the
        // datagram's fate is known.
        pending_qlog_packet_sents_.push_back(std::move(qlog_data));
        return;
    }

    // No datagram bracket (defensive): write immediately.
    QLOG_PACKET_SENT(qlog_trace_, qlog_data);
}

void SendControl::FlushPendingPacketSentsToQlog() {
    for (auto& data : pending_qlog_packet_sents_) {
        QLOG_PACKET_SENT(qlog_trace_, data);
    }
    pending_qlog_packet_sents_.clear();
}

void SendControl::DropPendingPacketSents() {
    pending_qlog_packet_sents_.clear();
}

std::function<void()> SendControl::MakeRetransmitTimeoutHandler(
    const std::shared_ptr<IPacket>& packet, uint32_t pkt_len, PacketNumberSpace ns) {
    return [this, pkt_len, packet, ns] {
        // NOTE: Do NOT call rtt_calculator_.OnPTOExpired() here.
        // PTO backoff is managed by the connection-level loss detection
        // timer to avoid exponential over-counting when multiple packets
        // time out together (e.g., packets 2-7 all lost → 6 callbacks →
        // pto_count_ jumps to 6).

        // Guard against double loss declaration: if DetectLostPackets()
        // already processed this packet (removed from unacked_packets_ or
        // marked is_lost), skip all operations to prevent double-counting
        // in congestion control (bytes_in_flight underflow).
        auto it = unacked_packets_[ns].find(packet->GetPacketNumber());
        if (it == unacked_packets_[ns].end() || it->second.is_lost) {
            return;
        }

        // RFC 9002 §5.3: loss detection is ACK-driven; a timeout without a new
        // ACK is not a loss event. Application-space recovery flows through
        // PTO probes + DetectLostPackets. Crypto spaces keep the timer-driven
        // re-queue (RFC 9000 §8.2.1 handshake recovery).
        if (ns == PacketNumberSpace::kApplicationNumberSpace) {
            return;
        }

        // RFC 9001 4.9.1/4.9.2: handshake confirmed — this space can never be
        // ACKed again. Drop the entry instead of declaring loss: a loss
        // declaration feeds lost_packets_, whose re-sends re-enter this map
        // and re-arm this timer — the self-sustaining probe storm. Return the
        // in-flight bytes via the discard path, not OnPacketLost (which would
        // wrongly cut the cwnd).
        if (handshake_complete_ &&
            (ns == PacketNumberSpace::kInitialNumberSpace || ns == PacketNumberSpace::kHandshakeNumberSpace)) {
            congestion_control_->OnPacketsDiscarded(pkt_len);
            unacked_packets_[ns].erase(it);
            return;
        }

        it->second.is_lost = true;
        // Frame-level delivery: this packet is declared lost, notify any
        // tracked frames so their producers re-emit fresh copies.
        FireFrameDelivery(it->second, FrameDeliveryState::kLost);
        // Carry the stream_data so the retransmitted PN re-inherits the same
        // byte-range tracking when the connection layer re-sends it.
        lost_packets_.push_back(LostPacketEntry{packet, it->second.stream_data});
        congestion_control_->OnPacketLost(LossEvent{packet->GetPacketNumber(), pkt_len, common::MonotonicTimeMsec() * 1000});

        // Metrics: Packet lost
        Metrics::CounterInc(common::MetricsStd::QuicPacketsLost);

        if (packet_lost_cb_) {
            packet_lost_cb_(packet);
        }
    };
}

void SendControl::TrackPacketForRetransmission(const std::shared_ptr<IPacket>& packet, uint32_t pkt_len,
    PacketNumberSpace ns, const std::vector<StreamDataInfo>& stream_data) {
    // RFC 9001 4.9.1/4.9.2: once the handshake is confirmed, Initial/Handshake
    // keys are (being) discarded on both sides, so packets in those spaces can
    // never be ACKed and must not enter loss recovery (their re-sends would
    // feed an eternal probe storm). Bytes were already counted as in-flight by
    // OnPacketSent(), so return them via the discard path (NOT OnPacketLost,
    // which would wrongly collapse the cwnd).
    if (handshake_complete_ &&
        (ns == PacketNumberSpace::kInitialNumberSpace || ns == PacketNumberSpace::kHandshakeNumberSpace)) {
        congestion_control_->OnPacketsDiscarded(pkt_len);
        LOG_INFO(
            "SendControl::TrackPacketForRetransmission: handshake confirmed, tracking-free final send at ns=%d "
            "pn=%llu len=%u",
            ns, (unsigned long long)packet->GetPacketNumber(), pkt_len);
        return;
    }

    // No per-packet timer: the record only feeds the threshold detectors and
    // the ACK walks; loss_detection_timer_ (updated below) decides when to
    // run them (RFC 9002 Appendix A).
    //
    // Per-packet retransmit timer (baseline recovery model, restored — see
    // PacketTimerInfo): the base PTO interval, NOT the backed-off one (backoff
    // belongs to the connection-level loss detection timer so a retransmission
    // storm stays bounded). Every space arms it; the handler itself only acts
    // on Initial/Handshake (App-space recovery is ACK/PTO-driven, RFC 9002
    // §5.3). RFC 9001 4.9.1/4.9.2 gate: after handshake confirmation these
    // spaces can never be ACKed again, so arming a timer would only feed the
    // probe storm — track-free, and return the bytes via the discard path.
    if (handshake_complete_ &&
        (ns == PacketNumberSpace::kInitialNumberSpace || ns == PacketNumberSpace::kHandshakeNumberSpace)) {
        congestion_control_->OnPacketsDiscarded(pkt_len);
        PacketTimerInfo info(largest_sent_time_[ns], pkt_len, stream_data);
        info.packet = packet;
        unacked_packets_[ns][packet->GetPacketNumber()] = std::move(info);
    } else {
        common::Timer retransmit_timer = scheduler_->AddTimer(life_token_,
            MakeRetransmitTimeoutHandler(packet, pkt_len, ns),
            static_cast<uint32_t>(rtt_calculator_.GetPTOInterval(GetEffectiveMaxAckDelay())));
        PacketTimerInfo info(largest_sent_time_[ns], pkt_len, std::move(retransmit_timer), stream_data);
        info.packet = packet;
        unacked_packets_[ns][packet->GetPacketNumber()] = std::move(info);
    }

    // RFC 9002 A.9: every outgoing ack-eliciting packet moves the PTO
    // deadline (earliest in-flight + PTO); Rearm splices the existing node,
    // which matters because this runs once per outgoing packet.
    SetLossDetectionTimer();
    LOG_DEBUG("SendControl::OnPacketSend: loss detection timer set, ns=%d pn=%llu unacked[%d]_size=%zu", ns,
        packet->GetPacketNumber(), ns, unacked_packets_[ns].size());
}

void SendControl::OnPacketAck(uint64_t now, PacketNumberSpace ns, const std::shared_ptr<IFrame>& frame) {
    if (frame->GetType() != FrameType::kAck && frame->GetType() != FrameType::kAckEcn) {
        LOG_ERROR("invalid frame on packet ack.");
        return;
    }

    // RFC 9002 §7.6: commit the previous ACK round's send-time anchor and
    // open a new one, so the persistent-congestion check below can prove
    // that packets sent between two lost packets were never acknowledged.
    if (round_ack_anchor_valid_) {
        recent_ack_anchors_.push_back({round_ack_min_send_time_, round_ack_max_send_time_});
        while (recent_ack_anchors_.size() > kMaxAckAnchors) {
            recent_ack_anchors_.pop_front();
        }
    }
    round_ack_anchor_valid_ = false;
    round_ack_min_send_time_ = 0;
    round_ack_max_send_time_ = 0;

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
    if (!ack_frame) {
        LOG_ERROR("SendControl::OnPacketAck: frame is not an AckFrame");
        return;
    }
    LOG_DEBUG("SendControl::OnPacketAck: largest_ack=%llu, first_ack_range=%llu, ns=%d", ack_frame->GetLargestAck(),
        ack_frame->GetFirstAckRange(), ns);

    // RFC 9000 §19.3.1 / §13.1: the peer must not acknowledge packet numbers we
    // never sent, and its ranges must be well formed. Validate before any walk:
    // the range lengths are attacker-controlled varints (up to 2^62-1) and
    // AckRangePackets() iterates once per acknowledged packet number.
    if (!ValidateAckFrame(ns, ack_frame)) {
        return;
    }

    EmitQlogPacketsAcked(ack_frame);
    AckLargestAckedPacket(now, ns, frame, ack_frame);
    AckRangePackets(now, ns, ack_frame);

    // P0-C3 (RFC 9000 §13.2.4 / RFC 9002 §7.4): ACK frames are never
    // retransmitted. Any packet of ours at or below the peer's largest_acked
    // is either acknowledged by this frame or permanently unacknowledged
    // (dropped) — either way its ack-of-ack bookkeeping can go. The entries
    // the walks above DID hit have already fired their callback and been
    // erased; this collapses the leftovers in the gaps.
    if (!ack_of_ack_[ns].empty()) {
        ack_of_ack_[ns].erase(ack_of_ack_[ns].begin(), ack_of_ack_[ns].upper_bound(ack_frame->GetLargestAck()));
    }

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

    // ACK delay (RFC 9000 §19.3: field value << peer's exponent is MICROseconds)
    data.ack_delay_us = ack_frame->GetAckDelay() << ack_delay_exponent_;

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

    // Metrics: ACK delay — scaled_ack_delay is in ms (see the caller), the
    // gauge is named ...Us, so convert. The old code stored the ms value
    // under a us name.
    Metrics::GaugeSet(common::MetricsStd::AckDelayUs, scaled_ack_delay * 1000);

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
    // P0-C3: run BEFORE the unacked lookup — packets carrying only an ACK
    // frame are not in unacked_packets_ (they are not retransmitted), yet the
    // peer acknowledging them is the ack-of-ack signal the receiver needs to
    // prune its re-report ranges.
    {
        auto afa = ack_of_ack_[ns].find(pkt_num);
        if (afa != ack_of_ack_[ns].end()) {
            if (ack_of_ack_cb_) {
                ack_of_ack_cb_(ns, afa->second);
            }
            ack_of_ack_[ns].erase(afa);
        }
    }
    auto task = unacked_packets_[ns].find(pkt_num);
    if (task == unacked_packets_[ns].end()) {
        return AckOneResult::kNotFound;
    }

    if (stream_log == StreamAckLogLevel::kPerStreamWithMissLog) {
        LOG_DEBUG("SendControl::OnPacketAck: found packet %llu, stream_data count=%zu", pkt_num,
            task->second.stream_data.size());
    }

    bool was_lost = task->second.is_lost;

    // RFC 9002 §7.6: this packet was acknowledged — its send time anchors
    // the "nothing in between was acked" check of persistent congestion.
    {
        uint64_t st = task->second.send_time_;
        if (!round_ack_anchor_valid_) {
            round_ack_anchor_valid_ = true;
            round_ack_min_send_time_ = st;
            round_ack_max_send_time_ = st;
        } else {
            if (st < round_ack_min_send_time_) round_ack_min_send_time_ = st;
            if (st > round_ack_max_send_time_) round_ack_max_send_time_ = st;
        }
    }

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

    // Capture the largest-acked packet number and send time BEFORE
    // AckOnePacket() below erases the entry: DetectLostPackets() (called
    // later in the same OnPacketAck) needs them for the RFC 9002 §6.1.2
    // time threshold and can no longer look them up in the map. The pair is
    // validated in DetectLostPackets(): a stale record (from a repeated or
    // out-of-order ACK) is not a valid anchor and disables the check for
    // that round.
    largest_acked_pn_[ns] = pkt_num;
    largest_acked_send_time_[ns] = iter->second.send_time_;

    // Scale peer-reported ACK delay by the PEER's advertised exponent.
    // RFC 9000 §19.3: the ACK Delay field encodes MICROSECONDS, so the
    // decoded value is in microseconds. The old code treated it as
    // milliseconds: since the field is (us >> exponent), interpreting it as
    // ms made nearly every sample ≥ the peer's max_ack_delay (ms) and the
    // clamp below pinned it there — inflating every RTT estimate by up to
    // max_ack_delay and delaying loss detection/PTO accordingly.
    uint64_t ack_delay_us = ack_frame->GetAckDelay() << ack_delay_exponent_;
    // RFC 9002 §5.3: the RTT adjustment MUST use min(ack_delay, the peer's
    // advertised max_ack_delay); otherwise a peer reporting an oversized ACK
    // Delay deflates SRTT/RTTVAR and drags the PTO early. (The handshake-
    // confirmed distinction — ignore max_ack_delay before confirmation — is
    // still tracked in the roadmap; clamping once the peer advertised a
    // value is the safe subset.) NOTE (#6, code review round 2):
    // max_ack_delay_ is seeded with kMaxAckDelay (25ms) in the constructor,
    // so the pre-transport-params handshake phase already clamps to 25ms — a
    // conservative default, not an unclamped window. UpdateConfig(
    // TransportParam) overwrites it with the peer's advertised value.
    // max_ack_delay_ is in MILLISECONDS — convert the cap to us before
    // comparing.
    if (max_ack_delay_ > 0 && ack_delay_us > max_ack_delay_ * 1000) {
        ack_delay_us = max_ack_delay_ * 1000;
    }
    uint64_t scaled_ack_delay = ack_delay_us / 1000;  // RttCalculator works in ms
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

void SendControl::ReportProtocolViolation(uint64_t error, uint16_t frame_type, const std::string& reason) {
    if (protocol_violation_cb_) {
        protocol_violation_cb_(error, frame_type, reason);
        return;
    }
    LOG_ERROR("SendControl: protocol violation (error=%llu, frame=%u): %s — no handler installed, dropping frame",
        static_cast<unsigned long long>(error), frame_type, reason.c_str());
}

bool SendControl::ValidateAckFrame(PacketNumberSpace ns, const std::shared_ptr<AckFrame>& ack_frame) {
    if (!ack_frame) {
        return false;
    }
    const uint64_t largest_ack = ack_frame->GetLargestAck();
    const uint64_t largest_sent = pkt_num_largest_sent_[ns];

    // RFC 9000 §19.3.1: "A receiver that receives an ACK frame ... MUST treat
    // the receipt of an ACK frame that acknowledges a packet number that was
    // never sent as a connection error of type PROTOCOL_VIOLATION."
    //
    // This is the check that makes the walk below safe. Without it a peer can
    // set Largest Acknowledged to an arbitrary 62-bit value and let
    // AckRangePackets() iterate once per "acknowledged" packet number.
    if (largest_ack > largest_sent) {
        LOG_ERROR("SendControl: ACK for un-sent packet number. largest_ack:%llu, largest_sent:%llu, ns:%d",
            static_cast<unsigned long long>(largest_ack), static_cast<unsigned long long>(largest_sent), ns);
        ReportProtocolViolation(
            QuicErrorCode::kProtocolViolation, FrameType::kAck, "acknowledged a packet number that was never sent");
        return false;
    }

    // First ACK Range: covers [largest_ack - first_range, largest_ack].
    const uint64_t first_range = ack_frame->GetFirstAckRange();
    if (first_range > largest_ack) {
        LOG_ERROR("SendControl: first ACK range underflows. first_range:%llu, largest_ack:%llu, ns:%d",
            static_cast<unsigned long long>(first_range), static_cast<unsigned long long>(largest_ack), ns);
        ReportProtocolViolation(
            QuicErrorCode::kFrameEncodingError, FrameType::kAck, "first ACK range exceeds largest acknowledged");
        return false;
    }

    // Additional ranges must march strictly downwards without underflowing
    // below packet number 0. `low` tracks the smallest PN covered so far.
    uint64_t low = largest_ack - first_range;
    for (const auto& range : ack_frame->GetAckRange()) {
        const uint64_t gap = range.GetGap();
        // gap + 2 can overflow only for gap > UINT64_MAX - 2, which the varint
        // decoder bounds to 2^62-1 — but compare defensively anyway.
        if (gap > UINT64_MAX - 2 || low < gap + 2) {
            LOG_ERROR("SendControl: ACK gap underflows. gap:%llu, low:%llu, ns:%d", static_cast<unsigned long long>(gap),
                static_cast<unsigned long long>(low), ns);
            ReportProtocolViolation(
                QuicErrorCode::kFrameEncodingError, FrameType::kAck, "ACK gap field underflows packet number 0");
            return false;
        }
        const uint64_t range_high = low - gap - 2;
        const uint64_t range_len = range.GetAckRangeLength();
        if (range_len > range_high) {
            LOG_ERROR(
                "SendControl: ACK range length underflows. len:%llu, range_high:%llu, ns:%d",
                static_cast<unsigned long long>(range_len), static_cast<unsigned long long>(range_high), ns);
            ReportProtocolViolation(
                QuicErrorCode::kFrameEncodingError, FrameType::kAck, "ACK range length underflows packet number 0");
            return false;
        }
        low = range_high - range_len;
    }

    return true;
}

void SendControl::AckRangePackets(uint64_t now, PacketNumberSpace ns, const std::shared_ptr<AckFrame>& ack_frame) {
    // Upper bound on how many packet numbers this one ACK frame may make us
    // walk. Every iteration is a hash lookup in unacked_packets_[ns], and a
    // peer-controlled range can nominally span up to 2^62 numbers, so without a
    // budget a single frame could monopolise the worker thread indefinitely.
    //
    // The budget is derived from what is actually outstanding rather than being
    // a fixed constant: packets still tracked in unacked_packets_ are the only
    // ones a walk can have any effect on, and they are always the *highest*
    // packet numbers, so a downward walk reaches all of them early. Anything
    // beyond the slack is guaranteed to be a kNotFound no-op, and those entries
    // remain recoverable through packet-threshold / PTO loss detection.
    auto& tracked = unacked_packets_[ns];
    const uint64_t walk_budget =
        std::max<uint64_t>(kAckWalkBudgetFloor, static_cast<uint64_t>(tracked.size()) * kAckWalkBudgetSlack);
    uint64_t walked = 0;
    bool budget_exhausted = false;

    // Returns false once the budget (or the tracked table) is exhausted.
    auto step = [&](uint64_t pkt_num, StreamAckLogLevel log_level) -> bool {
        if (tracked.empty()) {
            return false;  // nothing left that an ACK could possibly affect
        }
        if (walked >= walk_budget) {
            budget_exhausted = true;
            return false;
        }
        ++walked;
        if (AckOnePacket(ns, pkt_num, now, ack_frame->GetAckDelay(), false, log_level) == AckOneResult::kNotFound) {
            LOG_DEBUG("SendControl::OnPacketAck: packet %llu not found in unacked_packets", pkt_num);
        }
        return true;
    };

    uint64_t pkt_num = ack_frame->GetLargestAck();

    // Process first ACK range and notify streams
    // NOTE: largest_ack (pkt_num) was already processed above for RTT, so skip it
    // by starting from pkt_num-1
    //
    // ValidateAckFrame() has already proven first_range <= largest_ack, so
    // `pkt_num--` cannot wrap past 0 here.
    const uint64_t first_range = ack_frame->GetFirstAckRange();
    for (uint64_t i = 0; i < first_range; i++) {
        pkt_num--;  // Move to next packet in range
        if (!step(pkt_num, StreamAckLogLevel::kPerStreamWithMissLog)) {
            break;
        }
    }

    // Process additional ACK ranges
    for (auto iter = ack_frame->GetAckRange().begin(); iter != ack_frame->GetAckRange().end(); iter++) {
        if (walked >= walk_budget && !tracked.empty()) {
            break;
        }
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
        //
        // ValidateAckFrame() has already proven the subtraction cannot
        // underflow, so this mirrors the encode side exactly.
        pkt_num = pkt_num - iter->GetGap() - 2;
        const uint64_t range_len = iter->GetAckRangeLength();
        for (uint64_t i = 0; i <= range_len; i++) {
            if (!step(pkt_num, StreamAckLogLevel::kQuiet)) {
                return;
            }
            // BUGFIX P2-1: Only decrement pkt_num within the range, not after the last packet.
            // The extra decrement caused off-by-one for subsequent additional ranges:
            // next range would start at (lowest-1) instead of lowest.
            if (i < range_len) {
                pkt_num--;
            }
        }
    }

    if (budget_exhausted) {
        // Only reachable for a deliberately sparse frame; a normal ACK never
        // comes close. Log at DEBUG to avoid a per-packet cost.
        LOG_DEBUG("SendControl::AckRangePackets: walk budget %llu exhausted (ns=%d, tracked=%zu); "
                  "remaining entries fall back to loss detection",
            static_cast<unsigned long long>(walk_budget), ns, tracked.size());
        Metrics::CounterInc(common::MetricsStd::DiagAckWalkBudgetExhausted);
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

    // RFC 9002 §6.2.1 / A.9: restart the loss detection timer on every ACK.
    // SetLossDetectionTimer covers all three cases: pending time-threshold
    // deadline (from the DetectLostPackets run above), PTO while
    // ack-eliciting packets remain in flight in any space (unacked_packets_[]
    // only holds ack-eliciting packets), pre-handshake PING probing
    // (§6.2.2.1), and cancellation once nothing is in flight (§6.2.1).
    SetLossDetectionTimer();
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
    loss_detection_timer_.Cancel();
    for (int i = 0; i < PacketNumberSpace::kNumberSpaceCount; i++) {
        unacked_packets_[i].clear();
        loss_time_[i] = 0;
        ack_of_ack_[i].clear();
    }
}

void SendControl::FireFrameDelivery(PacketTimerInfo& info, FrameDeliveryState state) {
    // The tracked packet keeps its frames (IPacket::GetFrames), and each frame
    // keeps its optional handler, so re-queued / retransmitted copies of the
    // same IPacket stay tracked for free. Handlers are reset-style and
    // therefore safe to fire repeatedly (PTO can re-queue an already-lost
    // packet; the duplicate kLost is absorbed by the handler's idempotence).
    if (!info.packet) {
        LOG_DEBUG("SendControl::FireFrameDelivery: null packet, state=%d", (int)state);
        return;
    }
    LOG_DEBUG("SendControl::FireFrameDelivery: pn=%llu frames=%zu state=%d",
        (unsigned long long)info.packet->GetPacketNumber(), info.packet->GetFrames().size(), (int)state);
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
        if (!pair.second.is_lost) {
            discarded_bytes += pair.second.pkt_len_;
        }
    }
    if (discarded_bytes > 0) {
        congestion_control_->OnPacketsDiscarded(discarded_bytes);
    }
    unacked_packets_[ns].clear();
    loss_time_[ns] = 0;
    ack_of_ack_[ns].clear();
    // RFC 9002 A.11 OnPacketNumberSpaceDiscarded: also zero
    // time_of_last_ack_eliciting_packet (that send can never be ACKed now)
    // and reset the PTO backoff — key discard is forward progress.
    last_ack_eliciting_send_time_[ns] = 0;
    rtt_calculator_.OnPacketNumberSpaceDiscarded();
    SetLossDetectionTimer();  // deadline may have lived in the discarded space

    // Remove any lost packets from this space
    for (auto it = lost_packets_.begin(); it != lost_packets_.end();) {
        if (it->packet && CryptoLevel2PacketNumberSpace(it->packet->GetCryptoLevel()) == ns) {
            it = lost_packets_.erase(it);
        } else {
            ++it;
        }
    }
    // Do NOT zero pkt_num_largest_sent_ / pkt_num_largest_acked_ /
    // largest_sent_time_ here. RFC 9000 §4.10 discards the packet number
    // space, but the cipher keys are deliberately kept a while longer (pending
    // CRYPTO bytes may still need to be sent, see the comment in
    // BaseConnection::FinalizeHandshakePacketNumberSpaces). A peer's PTO
    // retransmission can therefore still deliver a *legitimate* Initial-space
    // ACK after the discard; zeroing the bookkeeping made ValidateAckFrame
    // reject it as "acknowledged a packet number that was never sent" and tear
    // down an otherwise healthy connection (observed in the handshakecorruption
    // interop run: largest_ack:3 vs largest_sent:0 right after handshake
    // confirmation). Historical values are monotonic and can only make the
    // validation more permissive for packets of a space we no longer use.

    LOG_INFO("SendControl: Discarded packet number space %d per RFC 9000", ns);
}

// Reset Initial packet state for Retry (clear unacked/lost packets, keep PN tracking)
void SendControl::ResetInitialPacketNumber() {
    PacketNumberSpace ns = PacketNumberSpace::kInitialNumberSpace;

    // Clear unacked packets for Initial space
    unacked_packets_[ns].clear();
    loss_time_[ns] = 0;
    SetLossDetectionTimer();

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

    // Send time of the largest-acked packet, for the time-threshold check.
    // A map lookup here would always miss: AckLargestAckedPacket() has
    // already erased the largest-acked entry from unacked_packets_ by the
    // time we run, which is why the time-threshold branch used to be dead
    // code. AckLargestAckedPacket() now captures the (pn, send_time) pair
    // before the erase, and the pair is only used when its packet number
    // matches THIS round's largest_acked. On a repeated or out-of-order ACK
    // frame (largest_acked <= last processed, so AckLargestAckedPacket()
    // early-returned) the stale reference is not a valid anchor — using it
    // could bias the threshold in either direction — so the time-threshold
    // check is disabled for that round; the packet threshold and PTO still
    // apply, and the check resumes on the next ACK that advances the
    // largest acknowledged. 0 disables the check.
    uint64_t largest_acked_send_time =
        (largest_acked_pn_[ns] == largest_acked) ? largest_acked_send_time_[ns] : 0;

    // Check all unacked packets with pkt_num < largest_acked
    std::vector<uint64_t> lost_packet_nums;
    // RFC 9002 §7.6: send times of THIS round's loss declarations (the real
    // loss branch only — handshake-space discards are not losses), used by
    // the persistent-congestion check below.
    std::vector<uint64_t> pc_lost_send_times;
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
        // (delta) OR more than loss_delay before now (#5, code review
        // round 2: the RFC specifies both conditions OR'd; only delta was
        // implemented). The now-branch also covers the rounds where the
        // largest-acked anchor is unavailable (out-of-order/repeated ACK ->
        // largest_acked_send_time == 0), so the time threshold no longer
        // goes blind in those windows.
        if (!should_declare_lost) {
            uint64_t time_since_sent =
                (largest_acked_send_time > info.send_time_) ? (largest_acked_send_time - info.send_time_) : 0;
            uint64_t age = (now > info.send_time_) ? (now - info.send_time_) : 0;
            if (time_since_sent > loss_delay || age > loss_delay) {
                should_declare_lost = true;
                LOG_DEBUG(
                    "DetectLostPackets: packet %llu lost by time threshold (delta=%llums, age=%llums, loss_delay=%llums)",
                    pkt_num, time_since_sent, age, loss_delay);
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
            // RFC 9001 4.9.1/4.9.2: handshake confirmed — Initial/Handshake
            // entries can never be ACKed again. Discard them (no loss
            // declaration, no retransmission queue) so the threshold
            // detector does not re-seed the probe storm; return the bytes
            // via the discard path instead of OnPacketLost.
            if (handshake_complete_ &&
                (ns == PacketNumberSpace::kInitialNumberSpace || ns == PacketNumberSpace::kHandshakeNumberSpace)) {
                congestion_control_->OnPacketsDiscarded(it->second.pkt_len_);
                LOG_INFO(
                    "SendControl::DetectLostPackets: handshake confirmed, discarding unacked entry ns=%d pn=%llu "
                    "instead of loss",
                    ns, (unsigned long long)pkt_num);
                unacked_packets_[ns].erase(it);
                continue;
            }
            pc_lost_send_times.push_back(it->second.send_time_);

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

    // RFC 9002 §7.6: persistent congestion is established on ACK-driven loss
    // rounds (the only round where a subsequent acked packet bounds the
    // window); timer-driven crypto-space losses skip it (conservative: no
    // persistent-congestion declaration without an ACK).
    if (!pc_lost_send_times.empty()) {
        MaybeDeclarePersistentCongestion(pc_lost_send_times);
    }

    // RFC 9002 A.9: earliest time-threshold deadline still pending in this
    // space — send_time + loss_delay of the oldest tracked packet below
    // largest_acked. Feeds SetLossDetectionTimer's kTimeThreshold mode: when
    // it expires, this detector runs again without waiting for the next ACK
    // (tail losses, crypto re-queuing). Requires an RTT sample; without one
    // loss_delay is a meaningless floor and PTO probing is the only sane
    // recovery path.
    loss_time_[ns] = 0;
    if (rtt_calculator_.GetSmoothedRtt() > 0) {
        for (auto& pair : unacked_packets_[ns]) {
            if (pair.first >= largest_acked) break;  // map is pn-ordered
            if (pair.second.is_lost) continue;       // declared already, not a future deadline
            loss_time_[ns] = pair.second.send_time_ + loss_delay;
            break;  // first surviving entry is the oldest (pn-ordered sends)
        }
    }

    // Log recovery metrics with sampling
    LogRecoveryMetricsIfChanged(now);
}

// RFC 9002 §7.6: declare persistent congestion when this round's lost
// packets span more than (srtt + max(4*rttvar, kGranularity) +
// max_ack_delay) * kPersistentCongestionThreshold AND no packet sent in
// between was acknowledged (checked against the acked send-time anchors) AND
// both window-bounding packets were sent after the first RTT sample.
void SendControl::MaybeDeclarePersistentCongestion(const std::vector<uint64_t>& lost_send_times) {
    // §7.6.2 (3) + Appendix B.8: only packets sent after the first RTT
    // sample participate. Pre-sample losses carry no trustworthy timing.
    uint64_t first_rtt_sample = rtt_calculator_.GetFirstRttSampleTime();
    if (first_rtt_sample == 0) {
        return;
    }

    uint64_t oldest = UINT64_MAX;
    uint64_t newest = 0;
    for (uint64_t st : lost_send_times) {
        if (st <= first_rtt_sample) {
            continue;
        }
        if (st < oldest) oldest = st;
        if (st > newest) newest = st;
    }
    // Need two distinct ack-eliciting lost packets bounding the window
    // (single candidate => zero span).
    if (oldest == UINT64_MAX || newest <= oldest) {
        return;
    }

    // §7.6.1: duration = (smoothed_rtt + max(4*rttvar, kGranularity) +
    // max_ack_delay) * kPersistentCongestionThreshold. GetPTOInterval is
    // exactly the parenthesized base (no backoff). Unlike the PTO timer, the
    // max_ack_delay term counts regardless of packet number space.
    uint64_t pc_duration =
        static_cast<uint64_t>(rtt_calculator_.GetPTOInterval(GetEffectiveMaxAckDelay())) * kPersistentCongestionThreshold;
    if (newest - oldest <= pc_duration) {
        return;
    }

    // §7.6.2 (1): no packet sent strictly between the two bounding packets
    // may have been acknowledged. Anchors hold [min,max] send times of
    // acked packets per ACK round; overlap with the open window
    // (oldest, newest) falsifies the condition.
    auto window_contains_acked = [oldest, newest](uint64_t amin, uint64_t amax) {
        return amin < newest && amax > oldest;
    };
    if (round_ack_anchor_valid_ && window_contains_acked(round_ack_min_send_time_, round_ack_max_send_time_)) {
        return;
    }
    for (const auto& anchor : recent_ack_anchors_) {
        if (window_contains_acked(anchor.min_send_time, anchor.max_send_time)) {
            return;
        }
    }

    // Declared: cwnd MUST collapse to the minimum congestion window and the
    // sender re-enters slow start (RFC 9002 §7.2/§7.6).
    congestion_control_->OnPersistentCongestion();
    Metrics::CounterInc(common::MetricsStd::CongestionEventsTotal);

    LOG_WARN(
        "SendControl: persistent congestion declared (RFC 9002 §7.6): %zu lost packets span %llums > %llums "
        "(srtt=%ums rttvar=%ums mad=%uu), collapsing cwnd",
        lost_send_times.size(), (unsigned long long)(newest - oldest), (unsigned long long)pc_duration,
        rtt_calculator_.GetSmoothedRtt(), rtt_calculator_.GetRttVar(), GetEffectiveMaxAckDelay());
}

// RFC 9002 Appendix A: the loss detection timer expired. Dispatches by the
// mode recorded in SetLossDetectionTimer: time-threshold expiry runs the loss
// detector for that space (the ONLY timer-driven loss declaration), PTO
// expiry sends probes (never a loss event, §6.2).
void SendControl::OnLossDetectionTimeout() {
    if (loss_detection_mode_ == LossDetectionMode::kTimeThreshold) {
        // Appendix A.5: a space has packets whose 9/8-RTT threshold passed
        // (largest_acked is unchanged — this covers tail losses that would
        // otherwise wait for the next ACK, and crypto re-queuing).
        PacketNumberSpace ns = loss_detection_ns_;
        LOG_INFO("SendControl::OnLossDetectionTimeout: time threshold expired ns=%d largest_acked=%llu, running "
                 "loss detection",
            ns, (unsigned long long)pkt_num_largest_acked_[ns]);
        DetectLostPackets(common::MonotonicTimeMsec(), ns, pkt_num_largest_acked_[ns]);
        SetLossDetectionTimer();
        return;
    }

    // RFC 9002 §6.2 PTO mode: increment backoff once per PTO firing (not per packet)
    rtt_calculator_.OnPTOExpired();

    LOG_WARN("SendControl::OnLossDetectionTimeout: PTO fired, pto_count=%u, triggering probe sc=%p",
        rtt_calculator_.GetConsecutivePTOCount(), (void*)this);
    LOG_DEBUG("SendControl::OnLossDetectionTimeout: PTO entry, unacked[0/1/2]={%zu,%zu,%zu} handshake_complete=%d",
        unacked_packets_[0].size(), unacked_packets_[1].size(), unacked_packets_[2].size(),
        handshake_complete_ ? 1 : 0);

    // RFC 9002 §6.2.4: Send probe packets to elicit ACK from peer
    // Trigger retransmission via the packet_lost_cb_ chain → ActiveSend → TrySend
    bool found_retransmit = false;
    if (packet_lost_cb_) {
        // RFC 9002 A.9 probes the space whose PTO expired first (A.8
        // GetPtoTimeAndSpace). A plain enum-order scan always picks Initial
        // when Initial and Handshake are both in flight, so the Handshake
        // certificate flight is never retransmitted: the client receives the
        // 90-byte Initial CRYPTO (ServerHello) over and over while its
        // handshake times out for lack of the certificate (bisect_B5b,
        // conn 39164061: client CH retransmissions arrive at 205/601/2829ms,
        // server answers only Initial ack+crypto, zero Handshake-level
        // retransmissions). Compute each candidate space's deadline with the
        // same now-based rule SetLossDetectionTimer uses, and start the scan
        // at the earliest one.
        auto probe_gate = [&](int ns) -> bool {
            if (handshake_complete_ &&
                (ns == PacketNumberSpace::kInitialNumberSpace || ns == PacketNumberSpace::kHandshakeNumberSpace)) {
                return false;
            }
            if (ns == PacketNumberSpace::kInitialNumberSpace && skip_initial_for_pto_) {
                for (const auto& entry : unacked_packets_[ns]) {
                    if (entry.second.packet && (entry.second.packet->GetFrameTypeBit() & FrameTypeBit::kCryptoBit)) {
                        return true;
                    }
                }
                return false;
            }
            return true;
        };
        int pto_first = -1;
        uint64_t pto_first_deadline = UINT64_MAX;
        for (int ns = 0; ns < PacketNumberSpace::kNumberSpaceCount; ns++) {
            if (unacked_packets_[ns].empty() || !probe_gate(ns)) {
                continue;
            }
            // Spaces with an older last-ack-eliciting send have an earlier
            // deadline; fall back to the enum order for ties (stable).
            uint64_t t = last_ack_eliciting_send_time_[ns];
            if (t < pto_first_deadline) {
                pto_first_deadline = t;
                pto_first = ns;
            }
        }
        int scan_order[PacketNumberSpace::kNumberSpaceCount];
        for (int i = 0; i < PacketNumberSpace::kNumberSpaceCount; i++) {
            scan_order[i] = (pto_first >= 0) ? ((pto_first + i) % PacketNumberSpace::kNumberSpaceCount) : i;
        }
        // Find the oldest unacked packet to probe with: a probe MUST produce
        // an ack-eliciting packet even when no loss has been declared yet
        // (§6.2: PTO is not a loss event, so unacked entries may all be
        // un-declared at this point).
        for (int scan_idx = 0; scan_idx < PacketNumberSpace::kNumberSpaceCount; scan_idx++) {
            const int ns = scan_order[scan_idx];
            // RFC 9001 4.9.1/4.9.2: once the handshake is confirmed the
            // Initial/Handshake keys are discarded on both sides, so nothing
            // in those spaces can ever be ACKed again — a PTO probe there is
            // pure poison on the wire (crosstraffic: eternal ~200pps
            // Handshake PING+CRYPTO probes). Defense in depth on top of
            // CryptoStream::MarkHandshakeConfirmed(), which drains the
            // re-queue engine at its source: any entry that still slips into
            // these spaces (e.g. a send already in flight when confirmation
            // landed) must not be probed.
            if (handshake_complete_ &&
                (ns == PacketNumberSpace::kInitialNumberSpace || ns == PacketNumberSpace::kHandshakeNumberSpace)) {
                continue;
            }
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

            // Application-space PTO probes used to be PING-only. That broke
            // STREAM recovery: SendStream has no frame-level kLost requeue
            // (unlike CryptoStream), so the lost_packets_ whole-packet path
            // was the ONLY retransmission channel for 1-RTT stream data —
            // reachable solely from DetectLostPackets, which needs an ACK.
            // With the response lost and the client's ACKs also lost, the
            // server pinged forever and never resent the data. RFC 9002
            // §6.2.2: a PTO probe SHOULD carry retransmitted unacknowledged
            // data. The oldest-first walk below plus the already-queued
            // dedup bounds this to one packet per cycle.

            // Zombie-entry fix. Entries declared lost (by the time threshold
            // or an earlier PTO) stay in unacked_packets_ until an ACK or
            // DetectLostPackets() removes them (see the "lost_packets_.empty()"
            // note below). Probing only begin() therefore gets stuck on the
            // oldest zombie forever: once it is marked is_lost, every later
            // PTO leaves found_retransmit false, no retransmission is queued,
            // and the connection stalls until the peer's idle timeout -- the
            // handshakecorruption failure against s2n-quic/quinn, where the
            // 1024-byte response was sent once, lost to the 30% corruption,
            // and never probed again. Walk past the zombies (in pn order --
            // unacked_packets_ is a std::map, P0-C4) and probe the oldest
            // packet not yet declared lost.
            // Drop entries whose packet is already gone. They can never be
            // probed, yet they keep unacked_packets_ non-empty -- which both
            // blocks the scan below and keeps the loss detection timer armed --
            // so the connection goes silent until the peer's idle timeout.
            for (auto cur = unacked.begin(); cur != unacked.end();) {
                if (!cur->second.packet) {
                    LOG_WARN("SendControl::OnLossDetectionTimeout: dropping unprobeable entry ns=%d pn=%llu (null packet)", ns,
                        (unsigned long long)cur->first);
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
                LOG_WARN("SendControl::OnLossDetectionTimeout: ns=%d all %zu entries already lost, re-queueing pn=%llu", ns,
                    unacked.size(), (unsigned long long)oldest->first);
                // PTO probe, not a loss event: no FireFrameDelivery here —
                // those frames were already notified at the real loss.
                lost_packets_.push_back(LostPacketEntry{oldest->second.packet, oldest->second.stream_data});
                packet_lost_cb_(oldest->second.packet);
                found_retransmit = true;
                // RFC 9002 A.9 sends probes synchronously, so the re-armed
                // deadline naturally counts from the probe (A.5 updates
                // time_of_last on send). Our egress is asynchronous; advancing
                // the per-space anchor here reproduces the RFC arming model —
                // retransmissions never remove the original from
                // unacked_packets_, so the anchor is what keeps the deadline
                // moving forward instead of degenerating to delay=0.
                last_ack_eliciting_send_time_[ns] = common::MonotonicTimeMsec();
                break;
            }

            // PTO probe (RFC 9002 §6.2.2): not a loss event — no is_lost, no
            // frame kLost, no congestion reaction. Loss declaration belongs to
            // the ACK-driven DetectLostPackets; this only re-queues the oldest
            // un-acked packet as the probe payload (a probe MAY carry
            // retransmitted data).
            // pto_probe=true is load-bearing: with no kLost fired, the
            // frame-level queues hold nothing, so this entry MUST survive
            // TrySendRetransmit's pure-CRYPTO drop and re-encode whole (see
            // LostPacketEntry::pto_probe).
            lost_packets_.push_back(LostPacketEntry{it->second.packet, it->second.stream_data, true});

            // Log marked_for_retransmit event (PTO-triggered)
            if (qlog_trace_) {
                common::MarkedForRetransmitData retransmit_data;
                retransmit_data.packet_number = it->first;
                retransmit_data.trigger = "pto_expired";
                QLOG_MARKED_FOR_RETRANSMIT(qlog_trace_, retransmit_data);
            }

            packet_lost_cb_(it->second.packet);
            found_retransmit = true;
            // Same RFC A.9 asynchronous-send adaptation as above: the probe
            // owns the next PTO interval of this space from now.
            last_ack_eliciting_send_time_[ns] = common::MonotonicTimeMsec();
            break;
        }
    }

    // RFC 9002 §6.2.2.1: During handshake, if no ACK-eliciting data to retransmit,
    // client MUST send a PING in Initial or Handshake space to elicit ACK from server
    //
    // lost_packets_.empty() guard (L1 "Expected 50. Got: 54"):
    // found_retransmit==false does NOT mean "nothing to retransmit". A
    // time-threshold expiry (or an earlier ACK round) can already have
    // declared packets lost and queued them into lost_packets_ without
    // erasing their unacked_packets_ entries — the probe scan's begin()
    // then lands on an already-is_lost entry, leaving found_retransmit
    // false even though a retransmission is queued.
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
        LOG_WARN("SendControl::OnLossDetectionTimeout: handshake not complete, no data to retransmit, sending probe");
        // RFC A.9 adaptation (see the retransmit arms): the probe owns the
        // next PTO interval of the fired space from now — probe_needed_cb_
        // egress is asynchronous, so without advancing the anchor the
        // deadline stays expired and every tick fires again (10-packet
        // Initial burst in one tick, handshakeloss server mode 10.28).
        last_ack_eliciting_send_time_[loss_detection_ns_] = common::MonotonicTimeMsec();
        probe_needed_cb_();
    }

    // RFC 9002 §6.2.4 completion: the guard above skips the immediate probe
    // whenever a retransmission is queued (found_retransmit / non-empty
    // lost_packets_), assuming it will put bytes on the wire. That assumption
    // does not hold for pre-handshake pure-CRYPTO entries: TrySendRetransmit
    // DROPS them (frame-level recovery owns the bytes) and when their CRYPTO
    // offsets were already acked through a later packet's delivery, the
    // frame-level queues stay empty too — the whole PTO round emits nothing.
    // Arm the deferred probe for that case; the send round consumes it only
    // if it ends with zero packets emitted (see ConsumePendingPtoProbe).
    if (found_retransmit && !handshake_complete_) {
        pto_probe_pending_ = true;
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
        LOG_WARN("SendControl::OnLossDetectionTimeout: post-handshake, scheduling PING probe (found_retransmit=%d)",
            found_retransmit ? 1 : 0);
        application_probe_cb_();
        // RFC A.9 adaptation (see the retransmit arm above): the PING probe
        // owns the next PTO interval of the Application space from now, even
        // if its actual egress is deferred to the next send round.
        last_ack_eliciting_send_time_[PacketNumberSpace::kApplicationNumberSpace] = common::MonotonicTimeMsec();
    }

    // Reschedule with updated backoff for the next probe.
    // Note this rearms from inside the timer callback itself. That is safe:
    // the node was parked before the callback ran, so Rearm re-links it.
    SetLossDetectionTimer();
}

// RFC 9002 Appendix A.9 SetLossDetectionTimer: one timer for the whole
// connection. (1) If any space has a pending time-threshold deadline
// (loss_time_), that is the earliest thing that can happen — arm in
// kTimeThreshold mode for the earliest space. (2) Otherwise arm a PTO
// deadline while ack-eliciting packets are in flight in any space that can
// still be ACKed; pre-handshake, keep it armed even with nothing in flight
// so a stalled handshake gets PING probes (§6.2.2.1). (3) Otherwise cancel.
// Backoff (kPto mode) lives on rtt_calculator_.GetPTOWithBackoff().
void SendControl::SetLossDetectionTimer() {
    if (!scheduler_) {
        return;
    }
    uint64_t now = common::MonotonicTimeMsec();

    // (1) earliest time-threshold deadline across spaces.
    uint64_t deadline = 0;
    PacketNumberSpace mode_ns = PacketNumberSpace::kApplicationNumberSpace;
    for (int ns = 0; ns < PacketNumberSpace::kNumberSpaceCount; ns++) {
        if (loss_time_[ns] != 0 && (deadline == 0 || loss_time_[ns] < deadline)) {
            deadline = loss_time_[ns];
            mode_ns = static_cast<PacketNumberSpace>(ns);
        }
    }

    uint64_t delay;
    if (deadline != 0) {
        loss_detection_mode_ = LossDetectionMode::kTimeThreshold;
        loss_detection_ns_ = mode_ns;
        delay = (deadline > now) ? (deadline - now) : 0;
    } else {
        // (2) PTO — RFC 9002 A.8 GetPtoTimeAndSpace. Note the base is the
        // PER-SPACE time_of_last_ack_eliciting_packet (A.3), and §6.2.1 says
        // a sender SHOULD restart the PTO timer on every ack-eliciting send.
        // Two historical bugs came from getting this wrong:
        //  - a global now-based base: any send in ANY space deferred the PTO
        //    of EVERY space, so the ~300ms keep-alive PING loop (Application
        //    Data) starved the PTO of a lost Handshake Finished forever
        //    (interop handshakeloss 10.27, client mode);
        //  - a global earliest-in-flight base: violates §6.2.1 (new sends
        //    must restart the interval) and, once the capped backoff no
        //    longer covered the oldest packet's age, armed at delay=0 — one
        //    probe per timer tick, a 10-packet Initial / 21-PING storm that
        //    burned the server's amplification budget (interop handshakeloss
        //    10.28, server mode).
        bool has_ack_eliciting_in_flight = false;
        uint64_t pto_timeout = UINT64_MAX;  // absolute deadline, earliest wins
        for (int ns = 0; ns < PacketNumberSpace::kNumberSpaceCount; ns++) {
            if (handshake_complete_ &&
                (ns == PacketNumberSpace::kInitialNumberSpace || ns == PacketNumberSpace::kHandshakeNumberSpace)) {
                continue;  // RFC 9001 4.9.1/4.9.2: keys discarded, never ACKable
            }
            if (unacked_packets_[ns].empty()) {
                continue;
            }
            has_ack_eliciting_in_flight = true;
            // BISECT: baseline semantics — the PTO interval counts from NOW
            // (every send/ACK/fire restarts the full interval, matching the
            // baseline's ArmPtoTimer(now + PTO) model). The strict A.8
            // anchor+PTO form armed deadlines that were already nearly due
            // whenever the last ack-eliciting send was old, making the timer
            // fire far more often than the baseline on lossy paths.
            uint64_t t = now + rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay());
            pto_timeout = std::min(pto_timeout, t);
        }
        if (!has_ack_eliciting_in_flight && handshake_complete_) {
            // (3) RFC 9002 §6.2.1: no ack-eliciting packets in flight and the
            // handshake is complete — nothing to detect.
            loss_detection_timer_.Cancel();
            return;
        }
        loss_detection_mode_ = LossDetectionMode::kPto;
        // A.8 anti-deadlock branch ("no ack-eliciting packets in flight"):
        // the PTO counts from now, because there is no send time to anchor
        // to — this is the pre-handshake stall probe (§6.2.2.1).
        if (!has_ack_eliciting_in_flight) {
            pto_timeout = now + rtt_calculator_.GetPTOWithBackoff(GetEffectiveMaxAckDelay());
        }
        delay = (pto_timeout > now) ? (pto_timeout - now) : 0;
    }

    // Rearm keeps the same node and the same handle, so the per-packet path
    // pays no allocation and no re-copy of the callback. It fails only when
    // there is no timer yet (or it was cancelled), in which case arm a new one.
    uint32_t delay_ms = static_cast<uint32_t>(delay);
    if (loss_detection_timer_.Rearm(delay_ms)) {
        return;
    }
    loss_detection_timer_ = scheduler_->AddTimer(life_token_, [this]() { OnLossDetectionTimeout(); }, delay_ms);
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
