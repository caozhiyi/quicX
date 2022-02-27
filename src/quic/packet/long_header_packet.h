
#ifndef QUIC_PACKET_LONG_HEADER_PACKET
#define QUIC_PACKET_LONG_HEADER_PACKET

#include <memory>

#include "type.h"
#include "packet_interface.h"

namespace quicx {

class Frame;
class Buffer;
class AlloterWrap;

static const uint8_t __connection_length_max = 20;

class LongHeaderPacket: public Packet {
public:
    LongHeaderPacket();
    virtual ~LongHeaderPacket();

    virtual bool Encode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual bool Decode(std::shared_ptr<Buffer> buffer, std::shared_ptr<AlloterWrap> alloter);
    virtual uint32_t EncodeSize();

    virtual bool AddFrame(std::shared_ptr<Frame> frame) = 0;

protected:
    union HeaderUnion {
        struct {
            uint8_t _header_form:1;
            uint8_t _fix_byte:1;
            uint8_t _packet_type:2;
            uint8_t _special_type:4;
        } _header_info;
        uint8_t _header;
    };

    HeaderUnion _header_format;
    uint32_t _version;

    uint8_t _destination_connection_id_length;
    char _destination_connection_id[__connection_length_max];

    uint8_t _source_connection_id_length;
    char _source_connection_id[__connection_length_max];
};

}

#endif