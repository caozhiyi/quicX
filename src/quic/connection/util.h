#ifndef QUIC_CONNECTION_CONTROLER_UTIL
#define QUIC_CONNECTION_CONTROLER_UTIL

#include <string>
#include <cstdint>
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

bool IsAckElictingPacket(uint32_t frame_type);

PacketNumberSpace CryptoLevel2PacketNumberSpace(uint16_t level);

const std::string FrameType2String(uint16_t frame_type);

}
}

#endif