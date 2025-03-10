#include <cstring>
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"
#include "quic/connection/controler/recv_control.h"

namespace quicx {
namespace quic {

RecvControl::RecvControl(std::shared_ptr<common::ITimer> timer):
    set_timer_(false),
    timer_(timer),
    max_ack_delay_(10) {
    memset(pkt_num_largest_recvd_, 0, sizeof(pkt_num_largest_recvd_));
    memset(largest_recv_time_, 0, sizeof(largest_recv_time_));

    timer_task_ = common::TimerTask([this] {
            set_timer_ = false;
            if (active_send_cb_) {
                active_send_cb_();
            }
        });
}

void RecvControl::OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet) {
    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        return;
    }

    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    if (pkt_num_largest_recvd_[ns] < packet->GetPacketNumber()) {
        pkt_num_largest_recvd_[ns] = packet->GetPacketNumber();
        largest_recv_time_[ns] = time;
    }

    wait_ack_packet_numbers_[ns].insert(packet->GetPacketNumber());
    if (!set_timer_) {
        set_timer_ = true;
        timer_->AddTimer(timer_task_, max_ack_delay_);
    }
}

std::shared_ptr<IFrame> RecvControl::MayGenerateAckFrame(uint64_t now, PacketNumberSpace ns) {
    if (set_timer_) {
        timer_->RmTimer(timer_task_);
        set_timer_ = false;
    }
    
    if (wait_ack_packet_numbers_[ns].empty()) {
        return nullptr;
    }

    std::shared_ptr<AckFrame> frame = std::make_shared<AckFrame>();
    frame->SetLargestAck(pkt_num_largest_recvd_[ns]);
    frame->SetAckDelay(now - largest_recv_time_[ns]);

    uint32_t first_ack_range = 0;

    uint64_t prev_pkt_num = pkt_num_largest_recvd_[ns];
    uint64_t prev_largest = prev_pkt_num;

    uint32_t gap = 0;
    uint32_t range = 0;
    for (auto iter = ++(wait_ack_packet_numbers_[ns].cbegin()); iter != wait_ack_packet_numbers_[ns].cend(); iter++) {
        if (prev_pkt_num == *iter + 1) {
            prev_pkt_num = *iter;
            range++;
            continue;
        }
        
        if (first_ack_range == 0) {
            first_ack_range = range;
            range = 0;
            // calc gap
            gap = prev_pkt_num - *iter + 1;

        } else {
            frame->AddAckRange(gap, range);
            range = 0;
        }

        prev_pkt_num = *iter;
        prev_largest = prev_pkt_num;
    }
    
    frame->SetFirstAckRange(first_ack_range);
    return frame;
}

void RecvControl::UpdateConfig(const TransportParam& tp) {
    max_ack_delay_ = tp.GetMaxAckDelay();
}

}
}
