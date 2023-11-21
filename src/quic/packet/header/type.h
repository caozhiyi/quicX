#ifndef QUIC_PACKET_HEADER_TYPE
#define QUIC_PACKET_HEADER_TYPE

namespace quicx {
namespace quic {

enum PacketHeaderType {
    PHT_SHORT_HEADER = 0x0,
    PHT_LONG_HEADER  = 0x1,
};

}
}

#endif