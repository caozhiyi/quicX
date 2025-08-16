#ifndef QUIC_PACKET_TYPE
#define QUIC_PACKET_TYPE

#include <cstdint>

namespace quicx {
namespace quic {

enum PacketType : uint16_t {
    kInitialPacketType     = 0x00,
    k0RttPacketType        = 0x01,
    kHandshakePacketType   = 0x02,
    kRetryPacketType       = 0x03,
    kNegotiationPacketType = 0x04,
    k1RttPacketType        = 0x05,

    kUnknownPacketType      = 0xFF,
};

enum PakcetCryptoLevel: uint16_t {
    kInitialCryptoLevel     = 0,
    kEarlyDataCryptoLevel   = 1,
    kHandshakeCryptoLevel   = 2,
    kApplicationCryptoLevel = 3,
    kUnknownCryptoLevel     = 4,
};

enum PacketNumberSpace: uint8_t {
    kInitialNumberSpace     = 0,
    kHandshakeNumberSpace   = 1,
    kApplicationNumberSpace = 2,

    kNumberSpaceCount       = 3,
};

static const uint32_t kRetryIntegrityTagLength = 128;

const char* PacketTypeToString(PacketType type);

}
}

#endif