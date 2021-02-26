
#ifndef QUIC_PACKET_RTT_1_PACKET
#define QUIC_PACKET_RTT_1_PACKET

#include "packet_interface.h"

namespace quicx {

class Rtt1Packet: public Packet {
public:
    Rtt1Packet();
    virtual ~Rtt1Packet();

private:
    union HeaderFormat {
        struct {
            uint8_t _header_from:1;
            uint8_t _fix_bit:1;
            uint8_t _spin_bit:1;
            uint8_t _reserved_bits:2;
            uint8_t _key_phase:1;
            uint8_t _packet_number_length:2;
        } _header_info;
        uint8_t _header;
    };

    HeaderFormat _header_format;

    char* _dest_connection_id;

    uint32_t _packet_number;
    char* _payload;
};

}

#endif