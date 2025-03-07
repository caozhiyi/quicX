#ifndef QUIC_UDP_UDP_RECEIVER
#define QUIC_UDP_UDP_RECEIVER

#include <queue>
#include <string>
#include <memory>
#include <cstdint>
#include "quic/udp/if_receiver.h"
#include "common/network/address.h"
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

    void AddReceiver(uint64_t socket_fd);
    void AddReceiver(const std::string& ip, uint16_t port);

    void TryRecv(std::shared_ptr<INetPacket> pkt, uint32_t timeout_ms);

    virtual void Wakeup();

private:
    bool TryRecv(std::shared_ptr<INetPacket> pkt);

private:
    std::queue<uint64_t> socket_queue_;
    std::shared_ptr<IUdpAction> action_;
};

}
}

#endif