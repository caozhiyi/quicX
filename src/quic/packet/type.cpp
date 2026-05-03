#include "quic/common/version.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

const char* PacketTypeToString(PacketType type) {
    switch (type)
    {
    case PacketType::kInitialPacketType:
        return "initial";
    case PacketType::k0RttPacketType:
        return "0rtt";
    case PacketType::kHandshakePacketType:
        return "handshake";
    case PacketType::kRetryPacketType:
        return "retry";
    case PacketType::kNegotiationPacketType:
        return "negotiation";
    case PacketType::k1RttPacketType:
        return "1rtt";
    default:
        return "unkonw";
    }
}

// RFC 9369 §3.2 Long Header Packet Type remapping for QUIC v2.
//   v2_bits = (v1_bits + 1) mod 4
//   v1_bits = (v2_bits + 3) mod 4   (== (v2_bits - 1) mod 4)
uint8_t MapPacketTypeToWire(PacketType type, uint32_t version) {
    // v1 wire bits are identical to the numeric enum value for the four
    // long-header packet types (Initial=0, 0-RTT=1, Handshake=2, Retry=3).
    uint8_t v1_bits = static_cast<uint8_t>(type) & 0x03;
    if (IsQuicV2(version)) {
        return static_cast<uint8_t>((v1_bits + 1) & 0x03);
    }
    return v1_bits;
}

PacketType MapWireToPacketType(uint8_t wire_bits, uint32_t version) {
    uint8_t v1_bits = wire_bits & 0x03;
    if (IsQuicV2(version)) {
        // Inverse of (+1 mod 4): (wire + 3) mod 4
        v1_bits = static_cast<uint8_t>((wire_bits + 3) & 0x03);
    }
    switch (v1_bits) {
        case 0: return PacketType::kInitialPacketType;
        case 1: return PacketType::k0RttPacketType;
        case 2: return PacketType::kHandshakePacketType;
        case 3: return PacketType::kRetryPacketType;
        default: return PacketType::kUnknownPacketType;
    }
}

}
}