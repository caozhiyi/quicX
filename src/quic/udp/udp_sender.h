#ifndef QUIC_UDP_UDP_SENDER
#define QUIC_UDP_UDP_SENDER

#include <string>
#include <cstdint>
#include "quic/udp/if_sender.h"
#include "quic/quicx/if_net_packet.h"

namespace quicx {
namespace quic {

class UdpSender:
    public ISender {
public:
    UdpSender();
    UdpSender(uint64_t sock);
    ~UdpSender() {}

    bool Send(std::shared_ptr<INetPacket>& pkt);

private:
    uint64_t _sock;
};

}
}

#endif