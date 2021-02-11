#ifndef QUIC_PACKET_PACKET_INTERFACE
#define QUIC_PACKET_PACKET_INTERFACE

#include <cstdint>


namespace quicx {

class Packet {
public:


private:
    union HeaderFormat {
        struct {
            uint8_t _header_type:1;
            uint8_t _header_fix_byte:1;
            uint8_t _packet_type:2;
            uint8_t _special_type:4;
        } _header_info;
        uint8_t _header;
    };

    HeaderFormat _header_format;
    uint32_t _version;

    uint8_t _dc_length;
    char* _dest_connection_id;

    uint8_t _sc_length;
    char* _src_connection_id;
};

}

#endif