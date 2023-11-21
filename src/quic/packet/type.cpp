#include "quic/packet/type.h"

namespace quicx {
namespace quic {

const char* PacketTypeToString(PacketType type) {
    switch (type)
    {
    case PT_INITIAL:
        return "initial";
    case PT_0RTT:
        return "0rtt";
    case PT_HANDSHAKE:
        return "handshake";
    case PT_RETRY:
        return "retry";
    case PT_NEGOTIATION:
        return "negotiation";
    case PT_1RTT:
        return "1rtt";
    default:
        return "unkonw";
    }
}

}
}