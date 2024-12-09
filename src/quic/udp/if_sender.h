#ifndef QUIC_UDP_IF_SENDER
#define QUIC_UDP_IF_SENDER

#include <string>
#include <cstdint>
#include "quic/quicx/if_net_packet.h"

namespace quicx {
namespace quic {

/*
 sender interface, send packet to network
*/
class ISender {
public:
    ISender() {}
    virtual ~ISender() {}

    virtual bool Send(std::shared_ptr<INetPacket>& pkt) = 0;
};

}
}

#endif