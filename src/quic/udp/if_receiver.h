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

    virtual void TryRecv(std::shared_ptr<INetPacket> pkt, uint32_t timeout_ms) = 0;

    virtual uint64_t GetRecvSocket() = 0;

    virtual void Weakup() = 0;
};

}
}

#endif