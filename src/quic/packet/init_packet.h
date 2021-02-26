
#ifndef QUIC_PACKET_INIT_PACKET
#define QUIC_PACKET_INIT_PACKET

#include "packet_interface.h"

namespace quicx {

class InitPacket: public Packet {
public:
    InitPacket();
    virtual ~InitPacket();

private:
    union HeaderFormat {
        struct {
            uint8_t _header_from:1;
            uint8_t _fix_bit:1;
            uint8_t _packet_type:2;
            uint8_t _reserved_bits:2;
            uint8_t _packet_number_length:2;
        } _header_info;
        uint8_t _header;
    };

    HeaderFormat _header_format;
    uint32_t _version;

    uint8_t _dc_length;
    char* _dest_connection_id;

    uint8_t _sc_length;
    char* _src_connection_id;

    uint32_t _toekn_length;
    char* _token;

    uint32_t _payload_length;
    uint32_t _packet_number;
    char* _payload;
};

}

#endif