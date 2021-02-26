#ifndef QUIC_PACKET_PACKET_HEADER_INTERFACE
#define QUIC_PACKET_PACKET_HEADER_INTERFACE

#include <cstdint>

namespace quicx {

class Packet {
public:
    Packet() {}
    virtual ~Packet() {}
};

}

#endif