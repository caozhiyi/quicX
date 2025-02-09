#ifndef QUIC_COMMON_CONSTANTS
#define QUIC_COMMON_CONSTANTS

#include <cstdint>

namespace quicx {
namespace quic {

// The maximum packet size of any QUIC packet over IPv6, based on ethernet's max
// size, minus the IP and UDP headers. IPv6 has a 40 byte header, UDP adds an
// additional 8 bytes.  This is a total overhead of 48 bytes.  Ethernet's
// max packet size is 1500 bytes,  1500 - 48 = 1452.
const uint16_t kMaxV6PacketSize = 1452;
// The maximum packet size of any QUIC packet over IPv4.
// 1500(Ethernet) - 20(IPv4 header) - 8(UDP header) = 1472.
const uint16_t kMaxV4PacketSize = 1472; 

const uint8_t kMaxConnectionLength = 20;
const uint8_t kMinConnectionLength = 8;

const uint8_t kInitialTlsTagLength = 16;
// RFC 9001, 5.4.1.  Header Protection Application: 5-byte mask
const uint8_t kHeaderProtectLength = 5;

}
}

#endif