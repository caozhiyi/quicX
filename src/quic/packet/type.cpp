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

}
}