#ifndef QUIC_UDP_IF_RECEIVER
#define QUIC_UDP_IF_RECEIVER

#include <string>
#include <cstdint>
#include "quic/quicx/if_net_packet.h"

namespace quicx {
namespace quic {

/*
 interface for receiving packets, try to receive a packet
*/
class IReceiver {
public:
    IReceiver() {}
    virtual ~IReceiver() {}

    enum RecvResult {
        RR_SUCCESS  = 0,
        RR_FAILED   = 1,
        RR_NO_DATA  = 2, // there is no packet to read
    };
    virtual RecvResult TryRecv(std::shared_ptr<INetPacket> pkt) = 0;

    virtual uint64_t GetRecvSocket() = 0;
};

}
}

#endif