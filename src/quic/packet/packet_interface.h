#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>

namespace quicx {

class Packet {
public:
    Packet() {}
    virtual ~Packet() {}
};

}

#endif