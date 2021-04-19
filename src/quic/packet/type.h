#ifndef QUIC_PACKET_TYPE
#define QUIC_PACKET_TYPE

#include <cstdint>

namespace qucix {

enum PacketType {
    PT_INITIAL   = 0x00,
    PT_0RTT      = 0x01,
    PT_HANDSHAKE = 0x02,
    PT_RETRY     = 0x03,
};

enum PacketHeaderType {
    PHT_LONG    = 0x00,
    PHT_SHORT   = 0x00,
};

static const uint16_t __fix_header_btye = 0x04;

enum PacketHeaderFormat: uint8_t {
  PHF_IETF_QUIC_LONG_HEADER_PACKET  = 0,
  PHF_IETF_QUIC_SHORT_HEADER_PACKET = 1,
  PHF_GOOGLE_QUIC_PACKET            = 2,
};

enum QuicLongHeaderType: uint8_t {
  QLHT_VERSION_NEGOTIATION = 0,
  QLHT_INITIAL             = 1,
  QLHT_ZERO_RTT_PROTECTED  = 2,
  QLHT_HANDSHAKE           = 3,
  QLHT_RETRY               = 4,

  QLHT_INVALID_PACKET_TYPE = 5,
};



}

#endif