#ifndef QUIC_PROCESS_RECEIVER_INTERFACE
#define QUIC_PROCESS_RECEIVER_INTERFACE

#include "quic/udp/udp_packet_in.h"

namespace quicx {

class IReceiver {
public:
    IReceiver() {}
    virtual ~IReceiver() {}

    virtual std::shared_ptr<UdpPacketIn> DoRecv() = 0;
};

}

#endif