#ifndef QUIC_PACKET_PACKET_HEADER
#define QUIC_PACKET_PACKET_HEADER

#include <cstdint>

namespace quicx {

static const uint8_t __connection_length_limit = 20;

class PacketHeaderInterface {
public:
    PacketHeader();
    ~PacketHeader();

    // Universal header. All QuicPacket headers will have a connection_id and
    // public flags.
    uint8_t _dc_length;
    char _dest_connection_id[__connection_length_limit];

    uint8_t _sc_length;
    char _src_connection_id[__connection_length_limit];

    // ********google quic********
    bool _reset_flag;
    // version flag in packets from the server means version
    // negotiation packet. For IETF QUIC, version flag means long header.
    bool version_flag;

    // ********google quic********
}

}

#endif