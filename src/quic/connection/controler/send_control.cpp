#include <cstring>
#include "common/log/log.h"
#include "quic/connection/util.h"
#include "quic/frame/ack_frame.h"
#include "quic/connection/controler/send_control.h"

namespace quicx {

SendControl::SendControl() {
    memset(_pkt_num_largest_sent, 0, sizeof(_pkt_num_largest_sent));
    memset(_pkt_num_largest_acked, 0, sizeof(_pkt_num_largest_acked));
    memset(_largest_sent_time, 0, sizeof(_largest_sent_time));
}

void SendControl::OnPacketSend(uint64_t time, std::shared_ptr<IPacket> packet) {
    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    _pkt_num_largest_sent[ns] = packet->GetPacketNumber();

    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        return;
    }
    _unacked_packets[ns][packet->GetPacketNumber()] = packet;
}

void SendControl::OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    if (frame->GetType() != FT_ACK) {
        LOG_ERROR("invalid frame on packet ack.");
        return;
    }
    
    auto ack_frame = std::dynamic_pointer_cast<AckFrame>(frame);
    uint64_t pkt_num = ack_frame->GetLargestAck();

    for (uint32_t i = 0; i <= ack_frame->GetFirstAckRange(); i++) {
        _unacked_packets[ns].erase(pkt_num--);
    }

    auto ranges = ack_frame->GetAckRange();
    for (auto iter = ranges.begin(); iter != ranges.end(); iter++) {
        pkt_num = pkt_num - iter->GetGap();
        for (uint32_t i = 0; i <= iter->GetAckRangeLength(); i++) {
            _unacked_packets[ns].erase(pkt_num--);
        }
    }
}

}