#ifndef QUIC_CONNECTION_CONTROLER_SEND_CONTROL
#define QUIC_CONNECTION_CONTROLER_SEND_CONTROL

#include <list>
#include <unordered_map>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"

namespace quicx {

// controller of sender. 
class SendControl {
public:
    SendControl();
    ~SendControl() {}

    void OnPacketSend(uint64_t time, std::shared_ptr<IPacket> packet);
    void OnPacketAck(uint64_t now, PacketNumberSpace ns, std::shared_ptr<IFrame> ack_frame);
    bool NeedReSend() { return !_lost_packets.empty(); }
    std::list<std::shared_ptr<IPacket>>& GetLostPacket() { return _lost_packets; }

private:
    std::list<std::shared_ptr<IPacket>> _lost_packets;
    std::unordered_map<uint64_t, std::shared_ptr<IPacket>> _unacked_packets[PNS_NUMBER];

    uint64_t _pkt_num_largest_sent[PNS_NUMBER];
    uint64_t _pkt_num_largest_acked[PNS_NUMBER];
    uint64_t _largest_sent_time[PNS_NUMBER];
};

}

#endif