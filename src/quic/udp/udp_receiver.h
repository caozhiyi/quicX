#ifndef QUIC_UDP_UDP_RECEIVER
#define QUIC_UDP_UDP_RECEIVER

#include <string>
#include <cstdint>
#include "quic/udp/if_receiver.h"
#include "common/network/address.h"

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
    UdpReceiver(uint64_t sock);
    // create a receiver with ip and port, may be used as a server
    UdpReceiver(const std::string& ip, uint16_t port);
    ~UdpReceiver();

    IReceiver::RecvResult TryRecv(std::shared_ptr<INetPacket> pkt);

    virtual uint64_t GetRecvSocket() { return _sock; }

private:
    uint64_t _sock;
    common::Address _local_address;
};

}
}

#endif