#ifndef QUIC_PACKET_GOOGLE_PACKET_HEADER
#define QUIC_PACKET_GOOGLE_PACKET_HEADER

#include "packet_header_interface.h"

namespace quicx {

class GooglePacketHeader:
    public PacketHeaderInterface {
public:
    GooglePacketHeader() {}
    virtual ~GooglePacketHeader() {}

    // ********google quic********
    bool _reset_flag;
    // version flag in packets from the server means version
    // negotiation packet. For IETF QUIC, version flag means long header.
    bool version_flag;

    // ********google quic********
}

}

#endif