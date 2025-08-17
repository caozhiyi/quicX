#ifndef QUIC_UDP_UDP_RECEIVER
#define QUIC_UDP_UDP_RECEIVER

#include <queue>
#include <string>
#include <memory>
#include <cstdint>

#include "quic/udp/if_receiver.h"
#include "quic/udp/action/if_udp_action.h"

namespace quicx {
namespace quic {

/*
 udp receiver, used to receive packets from udp socket
 we can process one connection in a single thread since set REUSE_PORT option to udp socket,
 a fix four set<source ip, source port, dest ip, dest port> is handled by one receiver udp socket.
*/
class UdpReceiver:
    public IReceiver {
public:
    // create a receiver with socket, may be used as a client
    UdpReceiver();
    ~UdpReceiver();

    void AddReceiver(uint64_t socket_fd) override;
    void AddReceiver(const std::string& ip, uint16_t port) override;

    void TryRecv(std::shared_ptr<NetPacket>& pkt, uint32_t timeout_ms) override;

    virtual void Wakeup() override;

    virtual void SetEcnEnabled(bool enabled) override { ecn_enabled_ = enabled; }

private:
    bool TryRecv(std::shared_ptr<NetPacket>& pkt);

private:
    bool ecn_enabled_;
    std::queue<uint64_t> socket_queue_;
    std::shared_ptr<IUdpAction> action_;
};

}
}

#endif