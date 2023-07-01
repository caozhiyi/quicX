#ifndef QUIC_CONNECTION_CONTROLER_UTIL
#define QUIC_CONNECTION_CONTROLER_UTIL

#include <cstdint>
#include "quic/packet/type.h"

namespace quicx {

bool IsAckElictingPacket(uint32_t frame_type);

PacketNumberSpace CryptoLevel2PacketNumberSpace(uint16_t level);

}

#endif