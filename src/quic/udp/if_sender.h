#ifndef QUIC_UDP_IF_SENDER
#define QUIC_UDP_IF_SENDER

#include <cstdint>
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

/*
 sender interface, send packet to network
*/
class ISender {
public:
    ISender() {}
    virtual ~ISender() {}

    virtual bool Send(std::shared_ptr<NetPacket>& pkt) = 0;

    virtual int32_t GetSocket() const = 0;

    static std::shared_ptr<ISender> MakeSender();
};

}
}

#endif
