#ifndef QUIC_PACKET_HEADER_TYPE
#define QUIC_PACKET_HEADER_TYPE

namespace quicx {

enum PacketHeaderType {
    PHT_SHORT_HEADER = 0x00,
    PHT_LONG_HEADER  = 0x01,
};

}

#endif