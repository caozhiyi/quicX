#ifndef QUIC_CONNECTION_CONTROLER_RECV_CONTROL
#define QUIC_CONNECTION_CONTROLER_RECV_CONTROL

#include <set>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"

namespace quicx {

class RecvControl {
public:
    RecvControl();
    ~RecvControl() {}

    void OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet);
    std::shared_ptr<IFrame> MayGenerateAckFrame(uint64_t now, PacketNumberSpace ns);

private:
    uint64_t _pkt_num_largest_recvd[PNS_NUMBER];
    uint64_t _largest_recv_time[PNS_NUMBER];

    std::set<uint64_t> _wait_ack_packet_numbers[PNS_NUMBER];
    
};

}

#endif