#ifndef QUIC_PACKET_TYPE
#define QUIC_PACKET_TYPE

#include <cstdint>

namespace qucix {

enum PacketType {
    PT_INITIAL   = 0x00,
    PT_0RTT      = 0x01,
    PT_HANDSHAKE = 0x02,
    PT_RETRY     = 0x03,
};

}

#endif