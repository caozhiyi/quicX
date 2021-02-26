
#ifndef QUIC_PACKET_RETRY_PACKET
#define QUIC_PACKET_RETRY_PACKET

#include "packet_interface.h"

namespace quicx {

class RetryPacket: public Packet {
public:
    RetryPacket();
    virtual ~RetryPacket();

private:
    union HeaderFormat {
        struct {
            uint8_t _header_from:1;
            uint8_t _fix_bit:1;
            uint8_t _packet_type:2;
            uint8_t _unused:4;
        } _header_info;
        uint8_t _header;
    };

    HeaderFormat _header_format;
    uint32_t _version;

    uint8_t _dc_length;
    char* _dest_connection_id;

    uint8_t _sc_length;
    char* _src_connection_id;

    char* _retry_token;
    char _retry_integrity_tag[128];
};

}

#endif