#ifndef QUIC_CONNECTION_CONTROLER_RECV_CONTROL
#define QUIC_CONNECTION_CONTROLER_RECV_CONTROL

#include <list>
#include "quic/packet/type.h"
#include "quic/packet/packet_interface.h"

namespace quicx {

class RecvControl {
public:
    RecvControl();
    ~RecvControl() {}

    void OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet);;

private:
    uint64_t _pkt_num_largest_recvd[PNS_NUMBER];
    uint64_t _largest_recv_time[PNS_NUMBER];
};

}

#endif