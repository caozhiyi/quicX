#ifndef QUIC_COMMON_CONSTANTS
#define QUIC_COMMON_CONSTANTS
#include <cstdint>

namespace quicx {

// The maximum packet size of any QUIC packet over IPv6, based on ethernet's max
// size, minus the IP and UDP headers. IPv6 has a 40 byte header, UDP adds an
// additional 8 bytes.  This is a total overhead of 48 bytes.  Ethernet's
// max packet size is 1500 bytes,  1500 - 48 = 1452.
const uint16_t __max_v6_packet_size = 1452;
// The maximum packet size of any QUIC packet over IPv4.
// 1500(Ethernet) - 20(IPv4 header) - 8(UDP header) = 1472.
const uint16_t __max_v4_packet_size = 1472; 

// maximum number of ranges per ACK frame
const uint16_t __max_ack_ranges = 10;

const uint8_t __max_connection_length = 20;
const uint8_t __min_connection_length = 8;

const uint8_t __long_header_form = 0x80; /* header form */
}

#endif