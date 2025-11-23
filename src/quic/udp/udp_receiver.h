#ifndef QUIC_UDP_UDP_RECEIVER
#define QUIC_UDP_UDP_RECEIVER

#include <string>
#include <memory>
#include <cstdint>

#include "quic/udp/if_receiver.h"
#include "common/network/if_event_loop.h"

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

    virtual bool AddReceiver(int32_t socket_fd, std::shared_ptr<IPacketReceiver> receiver) override;
    virtual bool AddReceiver(const std::string& ip, uint16_t port, std::shared_ptr<IPacketReceiver> receiver) override;

    virtual void SetEcnEnabled(bool enabled) override { ecn_enabled_ = enabled; }

protected:
    void OnRead(uint32_t fd) override;
    void OnWrite(uint32_t fd) override;
    void OnError(uint32_t fd) override;
    void OnClose(uint32_t fd) override;

private:
    bool TryRecv(std::shared_ptr<NetPacket>& pkt);

private:
    bool ecn_enabled_;
    std::shared_ptr<common::IEventLoop> event_loop_;
    std::unordered_map<int32_t, std::weak_ptr<IPacketReceiver>> receiver_map_;
};

}  // namespace quic
}  // namespace quicx

#endif