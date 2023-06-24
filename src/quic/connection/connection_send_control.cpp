#include <cstring>
#include "common/log/log.h"
#include "quic/connection/connection_send_control.h"

namespace quicx {

ConnectionSendControl::ConnectionSendControl() {
    memset(_pkt_num_largest_sent, 0, sizeof(_pkt_num_largest_sent));
    memset(_pkt_num_largest_recvd, 0, sizeof(_pkt_num_largest_recvd));
    memset(_pkt_num_largest_acked, 0, sizeof(_pkt_num_largest_acked));
    memset(_largest_sent_time, 0, sizeof(_largest_sent_time));
}

void ConnectionSendControl::OnPacketSend(uint64_t time, std::shared_ptr<IPacket> packet) {
    if (!IsAckElictingPacket(packet->GetFrameTypeBit())) {
        return;
    }
    
    auto ns = CryptoLevel2PacketNumberSpace(packet->GetCryptoLevel());
    _unacked_packets[ns].push_back(packet);
    _pkt_num_largest_sent[ns] = packet->GetPacketNumber();
}

void ConnectionSendControl::OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet) {
    
}

void ConnectionSendControl::OnPacketAck(uint64_t time, std::shared_ptr<IPacket> packet) {

}

bool ConnectionSendControl::IsAckElictingPacket(uint32_t frame_type) {
    return ((frame_type) & ~(FTB_ACK | FTB_ACK_ECN | FTB_PADDING | FTB_CONNECTION_CLOSE));
}

PacketNumberSpace ConnectionSendControl::CryptoLevel2PacketNumberSpace(uint16_t level) {
    switch (level) {
    case PCL_INITIAL: return PNS_INITIAL;
    case PCL_HANDSHAKE:  return PNS_HANDSHAKE;
    case PCL_ELAY_DATA:
    case PCL_APPLICATION: return PNS_APPLICATION;
    default:
        abort(); // TODO
    }
}

}