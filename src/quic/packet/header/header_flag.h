#ifndef QUIC_PACKET_HEADER_HEADER_FLAG
#define QUIC_PACKET_HEADER_HEADER_FLAG

#include <memory>
#include "quic/packet/type.h"
#include "quic/packet/header/type.h"
#include "common/buffer/if_buffer.h"

namespace quicx {
namespace quic {

struct LongHeaderFlag {
    uint8_t packet_number_length_:2; /*encryption protection*/ // must set when encode and decode
    uint8_t reserved_bits_:2;        /*encryption protection*/
    uint8_t packet_type_:2;
    uint8_t fix_bit_:1;
    uint8_t header_form_:1;

    uint8_t GetReservedBits() { return reserved_bits_; }
    void SetReservedBits(uint8_t bits) { reserved_bits_ = bits; }

    uint8_t GetPacketType() { return packet_type_; }
    void SetPacketType(uint8_t type) { packet_type_ = type; }
};

struct ShortHeaderFlag {
    uint8_t packet_number_length_:2; /*encryption protection*/
    uint8_t key_phase_:1;            /*encryption protection*/
    uint8_t reserved_bits_:2;        /*encryption protection*/
    uint8_t spin_bit_:1;
    uint8_t fix_bit_:1;
    uint8_t header_form_:1;

    uint8_t GetKeyPhase() { return key_phase_; }
    void SetKeyPhase(uint8_t phase) { key_phase_ = phase; }

    uint8_t GetReservedBits() { return reserved_bits_; }
    void SetReservedBits(uint8_t bits) { reserved_bits_ = bits; }

    uint8_t GetSpinBit() { return spin_bit_; }
    void SetSpinBit(uint8_t bit) { spin_bit_ = bit; }
};

class HeaderFlag {
public:
    HeaderFlag();
    HeaderFlag(PacketHeaderType type);
    HeaderFlag(uint8_t flag);
    virtual ~HeaderFlag() {}

    virtual bool EncodeFlag(std::shared_ptr<common::IBuffer> buffer);
    virtual bool DecodeFlag(std::shared_ptr<common::IBuffer> buffer);
    virtual uint32_t EncodeFlagSize();

    virtual PacketHeaderType GetHeaderType() const;
    uint8_t GetFixBit() const { return flag_.long_header_flag_.fix_bit_; } 

    virtual PacketType GetPacketType();

    uint8_t GetFlag() { return flag_.header_flag_; }

    uint8_t GetPacketNumberLength() { return flag_.long_header_flag_.packet_number_length_; }
    void SetPacketNumberLength(uint8_t len) { 
        flag_.long_header_flag_.packet_number_length_ = len;
    }
    LongHeaderFlag& GetLongHeaderFlag() { return flag_.long_header_flag_; }
    ShortHeaderFlag& GetShortHeaderFlag() { return flag_.short_header_flag_; }

protected:
    union HeaderFlagUnion {
        uint8_t header_flag_;
        LongHeaderFlag  long_header_flag_;
        ShortHeaderFlag short_header_flag_;
    } flag_;
};

}
}

#endif