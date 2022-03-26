#ifndef QUIC_PACKET_TYPE
#define QUIC_PACKET_TYPE

#include <stdint.h>

namespace quicx {

enum PacketType {
    PT_INITIAL     = 0x00,
    PT_0RTT        = 0x01,
    PT_HANDSHAKE   = 0x02,
    PT_RETRY       = 0x03,
    PT_NEGOTIATION = 0x04,
};

const uint8_t __max_connection_length = 20;
const uint8_t __min_connection_length = 8;

}

#endif