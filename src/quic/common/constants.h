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

// ----------------------------------------------------------------------------
// QUIC datagram / path constants (RFC 9000)
// ----------------------------------------------------------------------------

// RFC 9000 §14.1: Endpoints MUST be able to receive UDP datagrams of at least
// 1200 bytes; an Initial datagram MUST be padded to AT LEAST 1200 bytes;
// PATH_CHALLENGE / PATH_RESPONSE datagrams MUST also be padded to ≥1200 bytes
// (§8.2.1) so anti-amplification budget can authorize them on a fresh path.
//
// This is the universal "minimum-MTU floor" the protocol assumes — used as
// the initial PMTU before any probe succeeds, the padding target for Initials
// and probing packets, and the worker-side reject threshold for under-sized
// initial datagrams.
constexpr uint16_t kMinInitialPacketSize = 1200;

// RFC 9000 §8.2.4: An endpoint SHOULD abandon path validation based on
// timer. The timer SHOULD be at least three times the current PTO. We use a
// fixed 6 s default that is comfortably above 3×PTO for typical Internet
// paths (PTO ≈ a few hundred ms) and bounds how long a stuck migration can
// linger before we give up and either fall back to the prior path or close.
constexpr uint32_t kDefaultPathValidationTimeoutMs = 6000;

}
}

#endif
