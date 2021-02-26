#ifndef QUIC_PACKET_TYPE
#define QUIC_PACKET_TYPE

namespace qucix {

enum PacketType {
    PT_INITIAL   = 0x00,
    PT_0RTT      = 0x01,
    PT_HANDSHAKE = 0x02,
    PT_RETRY     = 0x03,
};

enum PacketHeaderType {
    PHT_LONG    = 0x00,
    PHT_SHORT   = 0x00,
};

static const unsigned int __fix_header_btye = 0x04;

}

#endif