#include <cstring>
#include "common/log/log.h"
#include "quic/connection/controler/util.h"
#include "quic/connection/controler/send_control.h"

namespace quicx {

SendControl::SendControl() {
    memset(_pkt_num_largest_sent, 0, sizeof(_pkt_num_largest_sent));
    memset(_pkt_num_largest_recvd, 0, sizeof(_pkt_num_largest_recvd));
    memset(_pkt_num_largest_acked, 0, sizeof(_pkt_num_largest_acked));
    memset(_largest_sent_time, 0, sizeof(_largest_sent_time));
}

void SendControl::OnPacketSend(uint64_t time, std::shared_ptr<IPacket> packet) {
    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        return;
    }
    
    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    _unacked_packets[ns].push_back(packet);
    _pkt_num_largest_sent[ns] = packet->GetPacketNumber();
}

void SendControl::OnPacketAck(uint64_t time, std::shared_ptr<IPacket> packet) {
    
}

}