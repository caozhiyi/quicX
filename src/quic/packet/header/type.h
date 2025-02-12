#ifndef QUIC_PACKET_HEADER_TYPE
#define QUIC_PACKET_HEADER_TYPE

namespace quicx {
namespace quic {

enum class PacketHeaderType {
    kShortHeader = 0x0,
    kLongHeader  = 0x1,
};

}
}

#endif