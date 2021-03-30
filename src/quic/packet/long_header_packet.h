
#ifndef QUIC_PACKET_LONG_HEADER_PACKET
#define QUIC_PACKET_LONG_HEADER_PACKET

#include "packet_interface.h"

namespace quicx {

static const uint8_t __connection_length_limit = 20;

class LongHeaderPacket: public Packet {
public:
    LongHeaderPacket();
    virtual ~LongHeaderPacket();

protected:
    union HeaderFormat {
        struct {
            uint8_t _header_form:1;
            uint8_t _fix_byte:1;
            uint8_t _packet_type:2;
            uint8_t _special_type:4;
        } _header_info;
        uint8_t _header;
    };

    HeaderFormat _header_format;
    uint32_t _version;

    uint8_t _dc_length;
    char _dest_connection_id[__connection_length_limit];

    uint8_t _sc_length;
    char _src_connection_id[__connection_length_limit];
};

}

#endif