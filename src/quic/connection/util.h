#ifndef QUIC_CONNECTION_CONTROLER_UTIL
#define QUIC_CONNECTION_CONTROLER_UTIL

#include <cstdint>
#include <string>
#include "common/network/address.h"
#include "quic/packet/type.h"

namespace quicx {
namespace quic {

bool IsAckElictingPacket(uint32_t frame_type);

PacketNumberSpace CryptoLevel2PacketNumberSpace(uint16_t level);

const std::string FrameType2String(uint16_t frame_type);

// Parses a peer-supplied preferred_address string of the form
// "<host>:<port>" or "[<ipv6>]:<port>" into an Address.
//
// A bare (unbracketed) IPv6 literal is rejected rather than guessed at:
// "::1:4433" could equally be host "::1" port 4433, or host "::1:4433"
// with no port, and picking one silently would be worse than refusing.
//
// @return true, and fills |out|, only if the entire string is a non-empty
//         host plus a port in [1, 65535].
bool ParsePreferredAddress(const std::string& value, common::Address& out);

// Validates only the RFC 9000 §18.2 binary layout of a preferred_address
// transport parameter (total length, CID length bound). Use this when the
// address-family choice is deferred to the caller; use
// ParsePreferredAddressBinary() to also extract an Address.
// @return true only if |raw| has the expected binary layout.
bool ValidatePreferredAddressBinary(const std::string& raw);

// Parses the RFC 9000 §18.2 binary preferred_address transport parameter
// (IPv4 + IPv6 addresses/ports, CID, stateless-reset token) into an Address.
// |peer_is_ipv4| selects which address family to return when both are present.
// |out_cid| (when non-null) receives the connection ID carried in the
// structure, per RFC 9000 §9.6.
// @return true only if |raw| has the expected binary layout.
bool ParsePreferredAddressBinary(const std::string& raw, bool peer_is_ipv4, common::Address& out,
                                 std::string* out_cid = nullptr);

}  // namespace quic
}  // namespace quicx

#endif