#ifndef QUIC_PACKET_TYPE
#define QUIC_PACKET_TYPE

#include <cstdint>

namespace quicx {
namespace quic {

enum PacketType : uint16_t {
    kInitialPacketType = 0x00,
    k0RttPacketType = 0x01,
    kHandshakePacketType = 0x02,
    kRetryPacketType = 0x03,
    kNegotiationPacketType = 0x04,
    k1RttPacketType = 0x05,

    kUnknownPacketType = 0xFF,
};

enum PacketCryptoLevel : uint16_t {
    kInitialCryptoLevel = 0,
    kEarlyDataCryptoLevel = 1,
    kHandshakeCryptoLevel = 2,
    kApplicationCryptoLevel = 3,
    kUnknownCryptoLevel = 4,
};

enum PacketNumberSpace : uint8_t {
    kInitialNumberSpace = 0,
    kHandshakeNumberSpace = 1,
    kApplicationNumberSpace = 2,

    kNumberSpaceCount = 3,
};

static const uint32_t kRetryIntegrityTagLength = 16;

const char* PacketTypeToString(PacketType type);

// RFC 9369 §3.2: QUIC v2 uses different wire values for the Long Header
// "Packet Type" (two-bit) field.
//   v1:  Initial=0b00, 0-RTT=0b01, Handshake=0b10, Retry=0b11
//   v2:  Initial=0b01, 0-RTT=0b10, Handshake=0b11, Retry=0b00
// Internally we keep PacketType enum values identical to the v1 wire bits
// (kInitialPacketType=0, k0RttPacketType=1, kHandshakePacketType=2,
//  kRetryPacketType=3) and only translate to/from the version-specific
// wire representation at the lowest (Long Header) encode/decode boundary.
//
// Returns the 2-bit wire value that should appear in the Long Header for the
// given logical packet type and QUIC version.
uint8_t MapPacketTypeToWire(PacketType type, uint32_t version);

// Returns the logical PacketType given the 2-bit wire value and QUIC version.
// For unknown versions the wire bits are treated as v1.
PacketType MapWireToPacketType(uint8_t wire_bits, uint32_t version);

}  // namespace quic
}  // namespace quicx

#endif