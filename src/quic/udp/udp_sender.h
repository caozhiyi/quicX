#ifndef QUIC_UDP_SENDER
#define QUIC_UDP_SENDER

#include <string>
#include <cstdint>
#include "quic/udp/udp_packet_out.h"

namespace quicx {

class UdpSender {
public:
    UdpSender() {}
    ~UdpSender() {}

    static bool DoSend(std::shared_ptr<UdpPacketOut> udp_packet);
};

}

#endif