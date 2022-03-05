
#ifndef QUIC_PACKET_SHORT_PACKET
#define QUIC_PACKET_SHORT_PACKET

#include <memory>
#include "quic/packet/packet_interface.h"

namespace quicx {

class ShortHeader: public Packet {
public:
    ShortHeader();
    virtual ~ShortHeader();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t EncodeSize();

protected:
    union HeaderUnion {
        struct {
            uint8_t _header_form:1;
            uint8_t _fix_bit:1;
            uint8_t _spin_bit:1;
            uint8_t _reserved_bits:2;
            uint8_t _key_phase:1;
            uint8_t _packet_number_length:2;
        } _header_info;
        uint8_t _header;
    };

    HeaderUnion _header_format;
    uint64_t _destination_connection_id;
    uint64_t _packet_number;
    char* _packet_payload;
};

}

#endif