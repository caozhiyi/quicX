
#ifndef QUIC_PACKET_LONG_PACKET
#define QUIC_PACKET_LONG_PACKET

#include <memory>
#include "quic/common/constants.h"
#include "quic/packet/packet_interface.h"

namespace quicx {

class LongHeader: public Packet {
public:
    LongHeader();
    virtual ~LongHeader();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t EncodeSize();

protected:
    union HeaderUnion {
        struct {
            uint8_t _header_form:1;
            uint8_t _fix_bit:1;
            uint8_t _packet_type:2;
            uint8_t _special_type:4;
        } _header_info;
        uint8_t _header;
    };

    HeaderUnion _header_format;
    uint32_t _version;

    uint8_t _destination_connection_id_length;
    char _destination_connection_id[__max_connection_length];

    uint8_t _source_connection_id_length;
    char _source_connection_id[__max_connection_length];
};

}

#endif