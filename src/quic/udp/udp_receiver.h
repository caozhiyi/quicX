#ifndef QUIC_UDP_UDP_RECEIVER
#define QUIC_UDP_UDP_RECEIVER

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/network/if_event_loop.h"

#include "quic/udp/if_receiver.h"

namespace quicx {
namespace quic {

/*
 udp receiver, used to receive packets from udp socket
 we can process one connection in a single thread since set REUSE_PORT option to udp socket,
 a fix four set<source ip, source port, dest ip, dest port> is handled by one receiver udp socket.
*/
class UdpReceiver: public IReceiver, public common::IFdHandler, public std::enable_shared_from_this<UdpReceiver> {
public:
    // create a receiver with socket, may be used as a client
    UdpReceiver(std::shared_ptr<common::IEventLoop> event_loop);
    ~UdpReceiver();

    virtual bool AddReceiver(common::SocketHandle sock, std::shared_ptr<IPacketReceiver> receiver) override;
    virtual bool AddReceiver(const std::string& ip, uint16_t port, std::shared_ptr<IPacketReceiver> receiver) override;
    virtual bool RemoveReceiver(int32_t socket_fd) override;

    virtual void SetEcnEnabled(bool enabled) override { ecn_enabled_ = enabled; }

protected:
    void OnRead(uint32_t fd) override;
    void OnWrite(uint32_t fd) override;
    void OnError(uint32_t fd) override;
    void OnClose(uint32_t fd) override;

private:
    bool TryRecv(std::shared_ptr<NetPacket>& pkt);

    // Pulls at most kMaxRecvBatch datagrams off `fd` and dispatches each one to
    // the registered IPacketReceiver. Returns the number of datagrams handled,
    // or -1 when the caller must stop draining (socket empty, allocator out of
    // clean buffers, fatal recv error, or receiver deregistered).
    int32_t DrainBatch(uint32_t fd);

private:
    bool ecn_enabled_;
    std::weak_ptr<common::IEventLoop> event_loop_;  // Observer reference (owner is QuicClient/QuicServer)
    // Registered entry: the receiver plus the socket handle whose family
    // travelled from the fd's creation site (socket_handle.h convention).
    // Storing the handle next to the receiver keeps fd -> family ownership
    // local to the registration entry: it dies with the entry, so a
    // closed-and-reused fd can never yield a stale family.
    struct Registered {
        std::weak_ptr<IPacketReceiver> receiver_;
        common::SocketHandle sock_;
    };
    std::unordered_map<int32_t, Registered> receiver_map_;
    // fds that were created internally by AddReceiver(ip, port, ...); the
    // receiver owns their lifecycle and must close them on teardown. fds that
    // were registered via AddReceiver(sock, ...) are owned by the caller and are
    // NOT tracked here (to avoid double-close).
    std::unordered_set<int32_t> owned_fds_;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_UDP_UDP_RECEIVER