#ifndef QUIC_PACKET_IETF_PACKET_HEADER
#define QUIC_PACKET_IETF_PACKET_HEADER

#include <string>
#include "packet_header_interface.h"

namespace quicx {

class IETFPacketHeader:
    public PacketHeaderInterface {
public:
    IETFPacketHeader() {}
    virtual ~IETFPacketHeader() {}

    // Length of the retry token length variable length integer field,
    // carried only by v99 IETF Initial packets.
    uint8_t _retry_token_length_length;
    // Retry token, carried only by v99 IETF Initial packets.
    std::string  _retry_token;
    // Length of the length variable length integer field,
    // carried only by v99 IETF Initial, 0-RTT and Handshake packets.
    uint8_t _length_length;
    // Length of the packet number and payload, carried only by v99 IETF Initial,
    // 0-RTT and Handshake packets. Also includes the length of the
    // diversification nonce in server to client 0-RTT packets.
    uint64_t remaining_packet_length;
}

}

#endif