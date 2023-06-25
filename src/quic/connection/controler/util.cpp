#include <cstdlib> // for abort

#include "quic/frame/type.h"
#include "quic/connection/controler/util.h"

namespace quicx {

bool IsAckElictingPacket(uint32_t frame_type);

PacketNumberSpace CryptoLevel2PacketNumberSpace(uint16_t level);

bool IsAckElictingPacket(uint32_t frame_type) {
    return ((frame_type) & ~(FTB_ACK | FTB_ACK_ECN | FTB_PADDING | FTB_CONNECTION_CLOSE));
}

PacketNumberSpace CryptoLevel2PacketNumberSpace(uint16_t level) {
    switch (level) {
    case PCL_INITIAL: return PNS_INITIAL;
    case PCL_HANDSHAKE:  return PNS_HANDSHAKE;
    case PCL_ELAY_DATA:
    case PCL_APPLICATION: return PNS_APPLICATION;
    default:
        abort(); // TODO
    }
}

}
