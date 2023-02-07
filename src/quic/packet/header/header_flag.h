#ifndef QUIC_PACKET_HEADER_HEADER_FLAG
#define QUIC_PACKET_HEADER_HEADER_FLAG

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/header/type.h"
#include "common/buffer/buffer_interface.h"

namespace quicx {

struct LongHeaderFlag {
    uint8_t _packet_number_length:2; /*encryption protection*/
    uint8_t _reserved_bits:2;        /*encryption protection*/
    uint8_t _packet_type:2;
    uint8_t _fix_bit:1;
    uint8_t _header_form:1;
};

struct ShortHeaderFlag {
    uint8_t _packet_number_length:2; /*encryption protection*/
    uint8_t _key_phase:1;            /*encryption protection*/
    uint8_t _reserved_bits:2;        /*encryption protection*/
    uint8_t _spin_bit:1;
    uint8_t _fix_bit:1;
    uint8_t _header_form:1;
};

class HeaderFlag {
public:
    HeaderFlag() { _flag._header_flag = 0; }
    HeaderFlag(uint8_t flag) { _flag._header_flag = flag; }
    virtual ~HeaderFlag() {}

    virtual bool EncodeFlag(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeFlag(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeFlagSize();

    uint8_t GetFlag() { return _flag._header_flag; }
    PacketHeaderType GetHeaderType() const;
    PacketType GetPacketType() const;
    LongHeaderFlag GetLongHeaderFlag() const { return _flag._long_header_flag; }
    ShortHeaderFlag GetShortHeaderFlag() const { return _flag._short_header_flag; }

protected:
    union HeaderFlagUnion {
        uint8_t _header_flag;
        LongHeaderFlag  _long_header_flag;
        ShortHeaderFlag _short_header_flag;
    } _flag;
};

}

#endif