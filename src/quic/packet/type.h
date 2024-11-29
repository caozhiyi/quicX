#ifndef QUICpacket_type_
#define QUICpacket_type_

#include <cstdint>

namespace quicx {
namespace quic {

enum PacketType {
    PT_INITIAL     = 0x00,
    PT_0RTT        = 0x01,
    PT_HANDSHAKE   = 0x02,
    PT_RETRY       = 0x03,
    PT_NEGOTIATION = 0x04,
    PT_1RTT        = 0x05,

    PT_UNKNOW      = 0xFF,
};

enum PakcetCryptoLevel: uint16_t {
    PCL_INITIAL     = 0,
    PCL_ELAY_DATA   = 1,
    PCL_HANDSHAKE   = 2,
    PCL_APPLICATION = 3,

    PCL_UNCRYPTO    = 4,
};

enum PacketNumberSpace: uint8_t {
    PNS_INITIAL     = 0,
    PNS_HANDSHAKE   = 1,
    PNS_APPLICATION = 2,

    PNS_NUMBER      = 3,
};

static const uint8_t __packent_number_length = 4;
static const uint32_t __retry_integrity_tag_length = 128;

const char* PacketTypeToString(PacketType type);

}
}

#endif