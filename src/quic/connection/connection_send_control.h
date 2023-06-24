#ifndef QUIC_CONNECTION_CONNECTION_SEND_CONTROL
#define QUIC_CONNECTION_CONNECTION_SEND_CONTROL

#include <list>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"

namespace quicx {

class ConnectionSendControl {
public:
    ConnectionSendControl();
    ~ConnectionSendControl() {}

    void OnPacketSend(uint64_t time, std::shared_ptr<IPacket> packet);
    void OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet);
    void OnPacketAck(uint64_t time, std::shared_ptr<IPacket> packet);
    bool NeedReSend() { return !_lost_packets.empty(); }
    std::list<std::shared_ptr<IPacket>>& GetLostPacket() { return _lost_packets; }

private:
    static bool IsAckElictingPacket(uint32_t frame_type);
    static PacketNumberSpace CryptoLevel2PacketNumberSpace(uint16_t level);

private:
    std::list<std::shared_ptr<IPacket>> _lost_packets;
    std::list<std::shared_ptr<IPacket>> _unacked_packets[PNS_NUMBER];

    uint64_t _pkt_num_largest_sent[PNS_NUMBER];
    uint64_t _pkt_num_largest_recvd[PNS_NUMBER];
    uint64_t _pkt_num_largest_acked[PNS_NUMBER];
    uint64_t _largest_sent_time[PNS_NUMBER];
};

}

#endif