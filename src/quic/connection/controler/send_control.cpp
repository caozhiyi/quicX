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

    congestion_control_->OnPacketSent(pkt_len, now);

    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        return;
    }
    auto timer_task = common::TimerTask([this, pkt_len, packet]{
        lost_packets_.push_back(packet);
        congestion_control_->OnPacketLost(pkt_len, common::UTCTimeMsec());
    });
    timer_->AddTimer(timer_task, rtt_calculator_.GetPT0Interval(max_ack_delay_));
    unacked_packets_[ns][packet->GetPacketNumber()] = PacketTimerInfo(largest_sent_time_[ns], pkt_len, timer_task);
}

void SendControl::OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    if (frame->GetType() != FrameType::kAck) {
        common::LOG_ERROR("invalid frame on packet ack.");
        return;
    }

    auto ack_frame = std::dynamic_pointer_cast<AckFrame>(frame);
    
    uint64_t pkt_num = ack_frame->GetLargestAck();
    if (pkt_num_largest_acked_[ns] < pkt_num) {
        pkt_num_largest_acked_[ns] = pkt_num;

        auto iter = unacked_packets_[ns].find(pkt_num);
        if (iter != unacked_packets_[ns].end()) {
            rtt_calculator_.UpdateRtt(iter->second.send_time_, now, ack_frame->GetAckDelay());
            congestion_control_->OnPacketAcked(iter->second.pkt_len_, now);
            congestion_control_->OnRttUpdated(rtt_calculator_.GetSmoothedRtt());
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
        pkt_num = pkt_num - iter->GetGap();
        for (uint32_t i = 0; i <= iter->GetAckRangeLength(); i++) {
            auto task = unacked_packets_[ns].find(pkt_num--);
            if (task != unacked_packets_[ns].end()) {
                timer_->RmTimer(task->second.timer_task_);
            }
        }
    }
}


void SendControl::CanSend(uint64_t now, uint32_t& can_send_bytes) {
    congestion_control_->CanSend(now, can_send_bytes);
}

void SendControl::UpdateConfig(const TransportParam& tp) {
    max_ack_delay_ = tp.GetMaxAckDelay();
}

}
}
