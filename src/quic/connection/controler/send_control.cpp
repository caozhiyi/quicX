#include <cstring>
#include "common/log/log.h"
#include "common/util/time.h"
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"
#include "quic/connection/controler/send_control.h"
#include "quic/congestion_control/congestion_control_factory.h"

namespace quicx {
namespace quic {

SendControl::SendControl(std::shared_ptr<common::ITimer> timer):
    timer_(timer),
    max_ack_delay_(10) {
    memset(pkt_num_largest_sent_, 0, sizeof(pkt_num_largest_sent_));
    memset(pkt_num_largest_acked_, 0, sizeof(pkt_num_largest_acked_));
    memset(largest_sent_time_, 0, sizeof(largest_sent_time_));
    congestion_control_ = CreateCongestionControl(CongestionControlType::kReno);
}

void SendControl::OnPacketSend(uint64_t now, std::shared_ptr<IPacket> packet, uint32_t pkt_len) {
    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    if (pkt_num_largest_sent_[ns] > packet->GetPacketNumber()) {
        common::LOG_ERROR("invalid packet number. number:%d", packet->GetPacketNumber());
        return;
    }
    pkt_num_largest_sent_[ns] = packet->GetPacketNumber();
    largest_sent_time_[ns] = common::UTCTimeMsec();

    congestion_control_->OnPacketSent(SentPacketEvent{packet->GetPacketNumber(), pkt_len, now, false});

    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        return;
    }
    auto timer_task = common::TimerTask([this, pkt_len, packet]{
        lost_packets_.push_back(packet);
        congestion_control_->OnPacketLost(LossEvent{packet->GetPacketNumber(), pkt_len, common::UTCTimeMsec()});
    });
    timer_->AddTimer(timer_task, rtt_calculator_.GetPT0Interval(max_ack_delay_));
    unacked_packets_[ns][packet->GetPacketNumber()] = PacketTimerInfo(largest_sent_time_[ns], pkt_len, timer_task);
}

void SendControl::OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    if (frame->GetType() != FrameType::kAck && frame->GetType() != FrameType::kAckEcn) {
        common::LOG_ERROR("invalid frame on packet ack.");
        return;
    }

    auto ack_frame = std::dynamic_pointer_cast<AckFrame>(frame);
    
    uint64_t pkt_num = ack_frame->GetLargestAck();
    if (pkt_num_largest_acked_[ns] < pkt_num) {
        pkt_num_largest_acked_[ns] = pkt_num;

        auto iter = unacked_packets_[ns].find(pkt_num);
        if (iter != unacked_packets_[ns].end()) {
            // Scale peer-reported ACK delay by exponent to milliseconds
            uint64_t scaled_ack_delay = ack_frame->GetAckDelay() << ack_delay_exponent_;
            rtt_calculator_.UpdateRtt(iter->second.send_time_, now, scaled_ack_delay);
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
                        state = EcnState::kValidated; // optimistic start
                    }
                    if (ect0 < prev_ect0 || ect1 < prev_ect1 || ce < prev_ce) {
                        state = EcnState::kFailed; // disable ECN responses if invalid
                    } else {
                        prev_ect0 = ect0;
                        prev_ect1 = ect1;
                        prev_ce = ce;
                        if (ce > 0) ecn_ce = true;
                    }
                }
            }
            congestion_control_->OnPacketAcked(AckEvent{pkt_num, iter->second.pkt_len_, now, ack_frame->GetAckDelay(), ecn_ce});
            congestion_control_->OnRoundTripSample(rtt_calculator_.GetSmoothedRtt(), ack_frame->GetAckDelay());
        }
    }

    for (uint32_t i = 0; i <= ack_frame->GetFirstAckRange(); i++) {
        auto task = unacked_packets_[ns].find(pkt_num--);
        if (task != unacked_packets_[ns].end()) {
            timer_->RmTimer(task->second.timer_task_);
        }
    }

    auto ranges = ack_frame->GetAckRange();
    for (auto iter = ranges.begin(); iter != ranges.end(); iter++) {
        // Move across the gap (unacked) and the single separator to the high PN of the next range
        pkt_num = pkt_num - iter->GetGap() - 1;
        for (uint32_t i = 0; i <= iter->GetAckRangeLength(); i++) {
            auto task = unacked_packets_[ns].find(pkt_num--);
            if (task != unacked_packets_[ns].end()) {
                timer_->RmTimer(task->second.timer_task_);
            }
        }
    }
}


void SendControl::CanSend(uint64_t now, uint64_t& can_send_bytes) {
    congestion_control_->CanSend(now, can_send_bytes);
}

void SendControl::UpdateConfig(const TransportParam& tp) {
    max_ack_delay_ = tp.GetMaxAckDelay();
    ack_delay_exponent_ = tp.GetackDelayExponent();
}

}
}
