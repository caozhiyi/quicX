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
    memset(ect0_count_, 0, sizeof(ect0_count_));
    memset(ect1_count_, 0, sizeof(ect1_count_));
    memset(ce_count_, 0, sizeof(ce_count_));

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

void RecvControl::OnEcnCounters(uint8_t ecn, PacketNumberSpace ns) {
    // ECN codepoints per RFC: 0b00 Not-ECT, 0b10 ECT(0), 0b01 ECT(1), 0b11 CE
    switch (ecn & 0x03) {
        case 0x02: // ECT(0)
            ++ect0_count_[ns];
            break;
        case 0x01: // ECT(1)
            ++ect1_count_[ns];
            break;
        case 0x03: // CE
            ++ce_count_[ns];
            break;
        default:
            break;
    }
}

std::shared_ptr<IFrame> RecvControl::MayGenerateAckFrame(uint64_t now, PacketNumberSpace ns, bool ecn_enabled) {
    if (set_timer_) {
        timer_->RmTimer(timer_task_);
        set_timer_ = false;
    }
    
    if (wait_ack_packet_numbers_[ns].empty()) {
        return nullptr;
    }

    // Generate ACK or ACK_ECN frame based on ECN enable
    std::shared_ptr<AckFrame> frame;
    if (ecn_enabled) {
        auto f = std::make_shared<AckEcnFrame>();
        f->SetEct0(ect0_count_[ns]);
        f->SetEct1(ect1_count_[ns]);
        f->SetEcnCe(ce_count_[ns]);
        frame = f;
    } else {
        frame = std::make_shared<AckFrame>();
    }
    frame->SetLargestAck(pkt_num_largest_recvd_[ns]);
    // encode ACK Delay in units of 2^ack_delay_exponent per RFC 9000
    {
        uint64_t delay_ms = now - largest_recv_time_[ns];
        uint64_t encoded = delay_ms >> ack_delay_exponent_;
        frame->SetAckDelay(static_cast<uint32_t>(encoded));
    }

    // Build ACK ranges from contiguous runs (descending by packet number)
    auto& nums = wait_ack_packet_numbers_[ns];
    if (!nums.empty()) {
        // Collect runs as [high, low]
        std::vector<std::pair<uint64_t, uint64_t>> runs;
        auto rit = nums.rbegin();
        uint64_t run_high = *rit;
        uint64_t run_low = run_high;
        ++rit;
        for (; rit != nums.rend(); ++rit) {
            uint64_t pn = *rit;
            if (pn + 1 == run_low) {
                // still contiguous downward
                run_low = pn;
            } else {
                // close current run and start a new one
                runs.emplace_back(run_high, run_low);
                run_high = pn;
                run_low = pn;
            }
        }
        // push last run
        runs.emplace_back(run_high, run_low);

        // First ACK Range is size of first run minus 1
        uint64_t first_ack_range = runs[0].first - runs[0].second;
        frame->SetFirstAckRange(static_cast<uint32_t>(first_ack_range));

        // Additional ranges: Gap and Range Len (len minus 1)
        for (size_t i = 1; i < runs.size(); ++i) {
            uint64_t prev_low = runs[i - 1].second;
            uint64_t next_high = runs[i].first;
            uint64_t gap = (prev_low - next_high) - 1;
            uint64_t range_len = runs[i].first - runs[i].second;
            frame->AddAckRange(gap, range_len);
        }
    } else {
        frame->SetFirstAckRange(0);
    }
    return frame;
}

void RecvControl::UpdateConfig(const TransportParam& tp) {
    max_ack_delay_ = tp.GetMaxAckDelay();
    ack_delay_exponent_ = tp.GetackDelayExponent();
}

}
}
