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

    uint8_t GetReservedBits() { return _reserved_bits; }
    void SetReservedBits(uint8_t bits) { _reserved_bits = bits; }

    uint8_t GetPacketType() { return _packet_type; }
    void SetPacketType(uint8_t type) { _packet_type = type; }
};

struct ShortHeaderFlag {
    uint8_t _packet_number_length:2; /*encryption protection*/
    uint8_t _key_phase:1;            /*encryption protection*/
    uint8_t _reserved_bits:2;        /*encryption protection*/
    uint8_t _spin_bit:1;
    uint8_t _fix_bit:1;
    uint8_t _header_form:1;

    uint8_t GetKeyPhase() { return _key_phase; }
    void SetKeyPhase(uint8_t phase) { _key_phase = phase; }

    uint8_t GetReservedBits() { return _reserved_bits; }
    void SetReservedBits(uint8_t bits) { _reserved_bits = bits; }

    uint8_t GetSpinBit() { return _spin_bit; }
    void SetSpinBit(uint8_t bit) { _spin_bit = bit; }
};

class HeaderFlag {
public:
    HeaderFlag() { _flag._header_flag = 0; }
    HeaderFlag(PacketHeaderType type) { _flag._header_flag = 0; _flag._long_header_flag.SetPacketType(type); }
    HeaderFlag(uint8_t flag) { _flag._header_flag = flag; }
    virtual ~HeaderFlag() {}

    virtual bool EncodeFlag(std::shared_ptr<IBufferWrite> buffer);
    virtual bool DecodeFlag(std::shared_ptr<IBufferRead> buffer);
    virtual uint32_t EncodeFlagSize();

    virtual PacketHeaderType GetHeaderType() const;
    virtual PacketType GetPacketType();

    uint8_t GetFlag() { return _flag._header_flag; }
    
    uint8_t GetPacketNumberLength() { return _flag._long_header_flag._packet_number_length; }
    void SetPacketNumberLength(uint8_t len) { _flag._long_header_flag._packet_number_length = len; }
    LongHeaderFlag& GetLongHeaderFlag() { return _flag._long_header_flag; }
    ShortHeaderFlag& GetShortHeaderFlag() { return _flag._short_header_flag; }

protected:
    union HeaderFlagUnion {
        uint8_t _header_flag;
        LongHeaderFlag  _long_header_flag;
        ShortHeaderFlag _short_header_flag;
    } _flag;
};

}

#endif