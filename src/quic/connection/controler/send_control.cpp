#include <cstring>

#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
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
    congestion_control_ = CreateCongestionControl(CongestionControlType::kReno);
}

void SendControl::OnPacketSend(uint64_t now, std::shared_ptr<IPacket> packet, uint32_t pkt_len) {
    OnPacketSend(now, packet, pkt_len, std::vector<StreamDataInfo>());
}

void SendControl::OnPacketSend(
    uint64_t now, std::shared_ptr<IPacket> packet, uint32_t pkt_len, const std::vector<StreamDataInfo>& stream_data) {
    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    common::LOG_DEBUG("SendControl::OnPacketSend: packet_number=%llu, ns=%d, frame_type_bit=%u, stream_data count=%zu",
        packet->GetPacketNumber(), ns, packet->GetFrameTypeBit(), stream_data.size());

    // Metrics: Packet transmitted
    common::Metrics::CounterInc(common::MetricsStd::QuicPacketsTx);

    if (pkt_num_largest_sent_[ns] > packet->GetPacketNumber()) {
        common::LOG_ERROR("invalid packet number. number:%llu", packet->GetPacketNumber());
        return;
    }
    pkt_num_largest_sent_[ns] = packet->GetPacketNumber();
    largest_sent_time_[ns] = common::UTCTimeMsec();

    // RFC 9002: Only ACK-eliciting packets count towards congestion control
    // ACK-only packets should NOT be accounted in bytes_in_flight
    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        common::LOG_DEBUG("SendControl::OnPacketSend: packet %llu is ACK-only, not counting in congestion control",
            packet->GetPacketNumber());
        return;
    }

    // Count this packet in congestion control (bytes_in_flight)
    congestion_control_->OnPacketSent(SentPacketEvent{packet->GetPacketNumber(), pkt_len, now, false});

    // Log packet_sent event to qlog
    if (qlog_trace_) {
        common::PacketSentData qlog_data;
        qlog_data.packet_number = packet->GetPacketNumber();
        qlog_data.packet_type = packet->GetHeader()->GetPacketType();
        qlog_data.packet_size = pkt_len;

        // Populate frame types from packet's frame type bits
        auto& frames = packet->GetFrames();
        for (const auto& frame : frames) {
            qlog_data.frames.push_back(static_cast<quic::FrameType>(frame->GetType()));
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
        lost_packets_.push_back(packet);
        congestion_control_->OnPacketLost(LossEvent{packet->GetPacketNumber(), pkt_len, common::UTCTimeMsec()});

        // Metrics: Packet lost
        common::Metrics::CounterInc(common::MetricsStd::QuicPacketsLost);

        if (packet_lost_cb_) {
            packet_lost_cb_(packet);
        }
    });
    // RFC 9002: Use PTO with exponential backoff
    timer_->AddTimer(timer_task, rtt_calculator_.GetPTOWithBackoff(max_ack_delay_));
    unacked_packets_[ns][packet->GetPacketNumber()] =
        PacketTimerInfo(largest_sent_time_[ns], pkt_len, timer_task, stream_data, packet);
    common::LOG_DEBUG(
        "SendControl::OnPacketSend: saved packet %llu to unacked_packets[%d], stream_data count=%zu, "
        "unacked_packets[%d] size=%zu",
        packet->GetPacketNumber(), ns, stream_data.size(), ns, unacked_packets_[ns].size());

    // RFC 9002: Schedule PTO timer to detect persistent timeouts
    // Cancel existing timer and reschedule with current PTO value
    timer_->RemoveTimer(pto_timer_);
    pto_timer_.SetTimeoutCallback(std::bind(&SendControl::OnPTOTimer, this));
    timer_->AddTimer(pto_timer_, rtt_calculator_.GetPTOWithBackoff(max_ack_delay_));
}

void SendControl::OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    if (frame->GetType() != FrameType::kAck && frame->GetType() != FrameType::kAckEcn) {
        common::LOG_ERROR("invalid frame on packet ack.");
        return;
    }

    auto ack_frame = std::dynamic_pointer_cast<AckFrame>(frame);
    common::LOG_DEBUG("SendControl::OnPacketAck: largest_ack=%llu, first_ack_range=%u, ns=%d",
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

        // Additional ACK ranges
        auto additional_ranges = ack_frame->GetAckRange();
        uint64_t current_pkt = largest - first_range - 1;  // After first range
        for (const auto& ack_range : additional_ranges) {
            current_pkt = current_pkt - ack_range.GetGap();  // Skip gap
            range.end = current_pkt;
            range.start = current_pkt - ack_range.GetAckRangeLength();
            data.ack_ranges.push_back(range);
            current_pkt = range.start - 1;  // Move to before this range
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
                common::LOG_WARN("Failed to update RTT for packet %llu", pkt_num);
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
                    AckEvent{pkt_num, iter->second.pkt_len_, now, ack_frame->GetAckDelay(), ecn_ce});
                congestion_control_->OnRoundTripSample(rtt_calculator_.GetSmoothedRtt(), ack_frame->GetAckDelay());

                // Metrics: Packet acknowledged
                common::Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
            }

            // Notify stream data ACK if callback is set
            if (stream_data_ack_cb_ && !iter->second.stream_data.empty()) {
                common::LOG_DEBUG("SendControl::OnPacketAck: notifying %zu streams for packet %llu",
                    iter->second.stream_data.size(), pkt_num);
                for (const auto& stream_info : iter->second.stream_data) {
                    common::LOG_DEBUG(
                        "SendControl::OnPacketAck: calling callback for stream_id=%llu, max_offset=%llu, has_fin=%d",
                        stream_info.stream_id, stream_info.max_offset, stream_info.has_fin);
                    stream_data_ack_cb_(stream_info.stream_id, stream_info.max_offset, stream_info.has_fin);
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
            common::LOG_DEBUG("SendControl::OnPacketAck: found packet %llu, stream_data count=%zu", pkt_num,
                task->second.stream_data.size());
            timer_->RemoveTimer(task->second.timer_task_);

            // Notify congestion control to decrement bytes_in_flight
            if (!task->second.is_lost) {
                congestion_control_->OnPacketAcked(
                    AckEvent{pkt_num, task->second.pkt_len_, now, ack_frame->GetAckDelay(), false});

                // Metrics: Packet acknowledged
                common::Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
            }

            // Notify stream data ACK if callback is set
            if (stream_data_ack_cb_ && !task->second.stream_data.empty()) {
                common::LOG_DEBUG("SendControl::OnPacketAck: notifying %zu streams for packet %llu",
                    task->second.stream_data.size(), pkt_num);
                for (const auto& stream_info : task->second.stream_data) {
                    common::LOG_DEBUG(
                        "SendControl::OnPacketAck: calling callback for stream_id=%llu, max_offset=%llu, has_fin=%d",
                        stream_info.stream_id, stream_info.max_offset, stream_info.has_fin);
                    stream_data_ack_cb_(stream_info.stream_id, stream_info.max_offset, stream_info.has_fin);
                }
            } else {
                common::LOG_DEBUG("SendControl::OnPacketAck: callback=%d, stream_data.empty()=%d",
                    stream_data_ack_cb_ ? 1 : 0, task->second.stream_data.empty());
            }

            // Remove from unacked_packets
            unacked_packets_[ns].erase(task);
        } else {
            common::LOG_DEBUG("SendControl::OnPacketAck: packet %llu not found in unacked_packets", pkt_num);
        }
    }

    // Process additional ACK ranges
    auto ranges = ack_frame->GetAckRange();
    for (auto iter = ranges.begin(); iter != ranges.end(); iter++) {
        // Move across the gap (unacked) and the single separator to the high PN of the next range
        pkt_num = pkt_num - iter->GetGap() - 1;
        for (uint32_t i = 0; i <= iter->GetAckRangeLength(); i++) {
            auto task = unacked_packets_[ns].find(pkt_num);
            if (task != unacked_packets_[ns].end()) {
                timer_->RemoveTimer(task->second.timer_task_);

                // Notify congestion control to decrement bytes_in_flight
                // BUG FIX: Was missing this call, causing bytes_in_flight to leak
                if (!task->second.is_lost) {
                    congestion_control_->OnPacketAcked(
                        AckEvent{pkt_num, task->second.pkt_len_, now, ack_frame->GetAckDelay(), false});

                    // Metrics: Packet acknowledged
                    common::Metrics::CounterInc(common::MetricsStd::QuicPacketsAcked);
                }

                // Notify stream data ACK if callback is set
                if (stream_data_ack_cb_ && !task->second.stream_data.empty()) {
                    for (const auto& stream_info : task->second.stream_data) {
                        stream_data_ack_cb_(stream_info.stream_id, stream_info.max_offset, stream_info.has_fin);
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

    // Cancel PTO timer since we received an ACK
    timer_->RemoveTimer(pto_timer_);

    // RFC 9002 §6.2.2.1: During handshake, keep PTO timer alive even after ACK
    // This ensures the client will send probes if the handshake stalls
    // (e.g., when server is anti-amplification limited)
    if (!handshake_complete_) {
        pto_timer_.SetTimeoutCallback(std::bind(&SendControl::OnPTOTimer, this));
        timer_->AddTimer(pto_timer_, rtt_calculator_.GetPTOWithBackoff(max_ack_delay_));
    }

    // Log recovery metrics with sampling
    LogRecoveryMetricsIfChanged(now);

    common::LOG_DEBUG("SendControl::OnPacketAck: completed for ns=%d, unacked_packets[%d] size=%zu", ns, ns,
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
        if (CryptoLevel2PacketNumberSpace((*it)->GetCryptoLevel()) == ns) {
            it = lost_packets_.erase(it);
        } else {
            ++it;
        }
    }
    pkt_num_largest_sent_[ns] = 0;
    pkt_num_largest_acked_[ns] = 0;
    largest_sent_time_[ns] = 0;

    common::LOG_INFO("SendControl: Discarded packet number space %d per RFC 9000", ns);
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
        if (CryptoLevel2PacketNumberSpace((*it)->GetCryptoLevel()) == ns) {
            it = lost_packets_.erase(it);
        } else {
            ++it;
        }
    }

    // Do NOT reset pkt_num_largest_sent_ / pkt_num_largest_acked_ / largest_sent_time_
    // The PN counter must continue incrementing after Retry per interop requirements

    common::LOG_INFO("SendControl: Reset Initial packet state for Retry (PN not reset)");
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
            common::LOG_DEBUG(
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
                common::LOG_DEBUG(
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

            // Add to lost_packets_ list for retransmission
            if (it->second.packet) {
                lost_packets_.push_back(it->second.packet);
            }

            // Notify congestion control
            congestion_control_->OnPacketLost(LossEvent{pkt_num, it->second.pkt_len_, now});

            // Metrics: Packet lost
            common::Metrics::CounterInc(common::MetricsStd::QuicPacketsLost);

            // Trigger retransmission callback
            if (packet_lost_cb_ && it->second.packet) {
                packet_lost_cb_(it->second.packet);
            }

            common::LOG_WARN("DetectLostPackets: declared packet %llu lost, triggering retransmission", pkt_num);

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
        common::LOG_INFO("DetectLostPackets: detected %zu lost packets in ns=%d", lost_packet_nums.size(), ns);
    }

    // Log recovery metrics with sampling
    LogRecoveryMetricsIfChanged(now);
}

// RFC 9002 §6.2: PTO timer callback - called when PTO expires without receiving ACK
void SendControl::OnPTOTimer() {
    // RFC 9002: Increment PTO backoff once per PTO firing (not per packet)
    rtt_calculator_.OnPTOExpired();

    common::LOG_WARN(
        "SendControl::OnPTOTimer: PTO fired, pto_count=%u, triggering probe", rtt_calculator_.GetConsecutivePTOCount());

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
                    lost_packets_.push_back(it->second.packet);
                    congestion_control_->OnPacketLost(LossEvent{it->first, it->second.pkt_len_, common::UTCTimeMsec()});
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
        common::LOG_WARN("SendControl::OnPTOTimer: handshake not complete, no data to retransmit, sending probe");
        probe_needed_cb_();
    }

    // Reschedule PTO timer with updated backoff for next probe
    timer_->RemoveTimer(pto_timer_);
    pto_timer_.SetTimeoutCallback(std::bind(&SendControl::OnPTOTimer, this));
    timer_->AddTimer(pto_timer_, rtt_calculator_.GetPTOWithBackoff(max_ack_delay_));
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
    data.pacing_rate_bps = congestion_control_->GetPacingRateBps();

    QLOG_METRICS_UPDATED(qlog_trace_, data);
}

}  // namespace quic
}  // namespace quicx
