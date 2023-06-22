#ifndef QUIC_UDP_LISTENER
#define QUIC_UDP_LISTENER

#include <string>
#include <cstdint>
#include "quic/udp/udp_packet_in.h"

namespace quicx {

class UdpListener {
public:
    UdpListener();
    ~UdpListener();

    bool Listen(const std::string& ip, uint16_t port);

    bool DoRecv(std::shared_ptr<UdpPacketIn> udp_packet);

private:
    uint64_t _listen_sock;
    Address _listen_address;
};

}

#endif