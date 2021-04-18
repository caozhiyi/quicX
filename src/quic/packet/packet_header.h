#ifndef QUIC_PACKET_PACKET_HEADER
#define QUIC_PACKET_PACKET_HEADER

#include <cstdint>

namespace quicx {

static const uint8_t __connection_length_limit = 20;

class PacketHeader {
public:
    PacketHeader();
    ~PacketHeader();

    uint8_t _dc_length;
    char _dest_connection_id[__connection_length_limit];

    uint8_t _sc_length;
    char _src_connection_id[__connection_length_limit];

}

}

#endif