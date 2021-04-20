#ifndef QUIC_PACKET_PACKET_HEADER_INTERFACE
#define QUIC_PACKET_PACKET_HEADER_INTERFACE

#include <cstdint>

namespace quicx {

enum PacketHeaderFormat : uint8_t {
  PHF_IETF_QUIC_LONG_HEADER_PACKET,
  PHF_IETF_QUIC_SHORT_HEADER_PACKET,
  PHF_GOOGLE_QUIC_PACKET,
};

static const uint8_t __connection_length_limit = 20;

class PacketHeaderInterface {
public:
    PacketHeaderInterface() {}
    virtual ~PacketHeaderInterface() {}

    // Universal header. All QuicPacket headers will have a connection_id and
    // public flags.
    uint8_t _dc_length;
    char _dest_connection_id[__connection_length_limit];

    uint8_t _sc_length;
    char _src_connection_id[__connection_length_limit];

    // Format of this header.
    PacketHeaderFormat _form;

    
}

}

#endif