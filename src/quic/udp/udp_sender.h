#ifndef QUIC_UDP_UDP_SENDER
#define QUIC_UDP_UDP_SENDER

#include <cstdint>

#include "quic/udp/if_sender.h"
#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

class UdpSender: public ISender {
public:
    UdpSender();
    // Legacy constructor for fds whose family is unknown (family 0 → the
    // send path falls back to ResolveSocketFamily, one syscall per send).
    UdpSender(int32_t sockfd);
    UdpSender(common::SocketHandle sock);
    ~UdpSender() {}

    bool Send(std::shared_ptr<NetPacket>& pkt) override;

    // sendmmsg-based batch send. See ISender::SendBatch for full contract.
    // Fast path: every packet's destination Address has a cached sockaddr
    // (filled in by a prior Send/SendTo on that Address) and they all share
    // the same socket fd -> a single sendmmsg(2) syscall. When any precondition
    // is not met (cache miss, mixed sockets, batch size exceeds
    // kMaxPacketsPerRound), the implementation transparently falls back to
    // per-packet Send(); cached state established by those Send() calls
    // makes subsequent rounds eligible for the fast path again.
    uint32_t SendBatch(std::vector<std::shared_ptr<NetPacket>>& batch) override;

    int32_t GetSocket() const override { return sock_.fd; }

private:
    // Fallback socket for packets that don't carry their own (fd <= 0).
    // Carries the creation-time family so the hot path pays no family
    // resolution syscall.
    common::SocketHandle sock_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_UDP_UDP_SENDER
