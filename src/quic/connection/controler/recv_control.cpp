#include <cstring>
#include "quic/connection/controler/recv_control.h"

namespace quicx {

RecvControl::RecvControl() {
    memset(_pkt_num_largest_recvd, 0, sizeof(_pkt_num_largest_recvd));
    memset(_largest_recv_time, 0, sizeof(_largest_recv_time));
}

void RecvControl::OnPacketRecv(uint64_t time, std::shared_ptr<IPacket> packet) {

}

}
