#ifndef QUIC_UDP_UDP_SENDER
#define QUIC_UDP_UDP_SENDER

#include <cstdint>
#include "quic/udp/if_sender.h"
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

class UdpSender:
    public ISender {
public:
    UdpSender();
    UdpSender(int32_t sockfd);
    ~UdpSender() {}

    bool Send(std::shared_ptr<NetPacket>& pkt);

    int32_t GetSocket() const { return sock_; }

private:
    int32_t sock_;
};

}
}

#endif