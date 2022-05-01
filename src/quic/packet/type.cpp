#include "quic/packet/type.h"

namespace quicx {

const char* PacketTypeToString(PacketType type) {
    switch (type)
    {
    case PT_INITIAL:
        return "initial";
    case PT_0RTT:
        return "0rtt";
    case PT_HANDSHAKE:
        return "handshacke";
    case PT_RETRY:
        return "retry";
    case PT_NEGOTIATION:
        return "negotiation";
    default:
        return "unkonw";
    }
}

}
