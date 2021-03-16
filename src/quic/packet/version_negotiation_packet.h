
#ifndef QUIC_PACKET_VERSION_CONSULT_PACKET
#define QUIC_PACKET_VERSION_CONSULT_PACKET

#include <vector>
#include "packet_interface.h"

namespace quicx {

class VersionNegotiationPacket: public Packet {
public:
    VersionNegotiationPacket();
    virtual ~VersionNegotiationPacket();

private:
    union HeaderFormat {
        struct {
            uint8_t _header_from:1;
            uint8_t _header_unused:7;
        } _header_info;
        uint8_t _header;
    };

    HeaderFormat _header_format;
    uint32_t _version;

    uint8_t _dc_length;
    char* _dest_connection_id;

    uint8_t _sc_length;
    char* _src_connection_id;

    std::vector<uint32_t> _support_version;
};

}

#endif