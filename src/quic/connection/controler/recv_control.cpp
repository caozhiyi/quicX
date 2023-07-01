#include <cstring>
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"
#include "quic/connection/controler/recv_control.h"

namespace quicx {

RecvControl::RecvControl() {
    memset(_pkt_num_largest_recvd, 0, sizeof(_pkt_num_largest_recvd));
    memset(_largest_recv_time, 0, sizeof(_largest_recv_time));
}

void RecvControl::OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet) {
    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        return;
    }

    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    if (_pkt_num_largest_recvd[ns] < packet->GetPacketNumber()) {
        _pkt_num_largest_recvd[ns] = packet->GetPacketNumber();
        _largest_recv_time[ns] = time;
    }

    _wait_ack_packet_numbers[ns].insert(packet->GetPacketNumber());
}

std::shared_ptr<IFrame> RecvControl::MayGenerateAckFrame(uint64_t now, PacketNumberSpace ns) {
    if (_wait_ack_packet_numbers[ns].empty()) {
        return nullptr;
    }
    
    std::shared_ptr<AckFrame> frame = std::make_shared<AckFrame>();
    frame->SetLargestAck(_pkt_num_largest_recvd[ns]);
    frame->SetAckDelay(now - _largest_recv_time[ns]);

    uint32_t first_ack_range = 0;

    uint64_t prev_pkt_num = _pkt_num_largest_recvd[ns];
    uint64_t prev_largest = prev_pkt_num;

    uint32_t gap = 0;
    uint32_t range = 0;
    for (auto iter = ++(_wait_ack_packet_numbers[ns].cbegin()); iter != _wait_ack_packet_numbers[ns].cend(); iter++) {
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

}
