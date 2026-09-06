#ifndef QUIC_UDP_IF_RECEIVER
#define QUIC_UDP_IF_RECEIVER

#include <cstdint>
#include <string>

#include "common/network/if_event_loop.h"

#include "quic/udp/net_packet.h"

namespace quicx {
namespace quic {

class IPacketReceiver {
public:
    IPacketReceiver() {}
    virtual ~IPacketReceiver() {}

    virtual void OnPacket(std::shared_ptr<NetPacket>& pkt) = 0;
};

/*
 interface for receiving packets, try to receive a packet
*/
class IReceiver {
public:
    IReceiver() {}
    virtual ~IReceiver() {}

    // Register an externally-owned socket. The handle's family must come from
    // the fd's creation site (see common/network/socket_handle.h); passing 0
    // forces a one-time ResolveSocketFamily probe at registration.
    virtual bool AddReceiver(common::SocketHandle sock, std::shared_ptr<IPacketReceiver> receiver) = 0;

    // add a receiver, return the socket fd
    virtual bool AddReceiver(const std::string& ip, uint16_t port, std::shared_ptr<IPacketReceiver> receiver) = 0;

    // Remove a receiver by socket file descriptor
    virtual bool RemoveReceiver(int32_t socket_fd) = 0;

    // Enable or disable ECN features on underlying sockets created/managed by receiver
    virtual void SetEcnEnabled(bool enabled) = 0;

    static std::shared_ptr<IReceiver> MakeReceiver(std::shared_ptr<common::IEventLoop> event_loop);
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_UDP_IF_RECEIVER