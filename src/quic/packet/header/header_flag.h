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

    uint8_t getPacketNumberLength() { return _packet_number_length; }
    void setPacketNumberLength(uint8_t len) { return _packet_number_length = len; }

    uint8_t getReservedBits() { return _reserved_bits; }
    void setReservedBits(uint8_t bits) { return _reserved_bits = bits; }

    uint8_t getPacketType() { return _packet_type; }
    void setPacketType(uint8_t type) { return _packet_type = type; }
};

struct ShortHeaderFlag {
    uint8_t _packet_number_length:2; /*encryption protection*/
    uint8_t _key_phase:1;            /*encryption protection*/
    uint8_t _reserved_bits:2;        /*encryption protection*/
    uint8_t _spin_bit:1;
    uint8_t _fix_bit:1;
    uint8_t _header_form:1;

    uint8_t getPacketNumberLength() { return _packet_number_length; }
    void setPacketNumberLength(uint8_t len) { return _packet_number_length = len; }

    uint8_t getKeyPhase() { return _key_phase; }
    void setKeyPhase(uint8_t phase) { return _key_phase = phase; }

    uint8_t getReservedBits() { return _reserved_bits; }
    void setReservedBits(uint8_t bits) { return _reserved_bits = bits; }

    uint8_t getSpinBit() { return _spin_bit; }
    void setSpinBit(uint8_t bit) { return _spin_bit = bit; }
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
    LongHeaderFlag& GetLongHeaderFlag() const { return _flag._long_header_flag; }
    ShortHeaderFlag& GetShortHeaderFlag() const { return _flag._short_header_flag; }

protected:
    union HeaderFlagUnion {
        uint8_t _header_flag;
        LongHeaderFlag  _long_header_flag;
        ShortHeaderFlag _short_header_flag;
    } _flag;
};

}

#endif