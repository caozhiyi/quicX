#ifndef QUIC_PACKET_HEADER_FLAG
#define QUIC_PACKET_HEADER_FLAG

#include <memory>
#include "common/buffer/buffer_interface.h"

namespace quicx {

struct LongHeaderFlag {
    uint8_t _special_type:4;
    uint8_t _packet_type:2;
    uint8_t _fix_bit:1;
    uint8_t _header_form:1;
};

struct ShortHeaderFlag {
    uint8_t _packet_number_length:2;
    uint8_t _key_phase:1;
    uint8_t _reserved_bits:2;
    uint8_t _spin_bit:1;
    uint8_t _fix_bit:1;
    uint8_t _header_form:1;
};

class HeaderFlag {
public:
    HeaderFlag(uint8_t flag) { _flag._header_flag = flag; }
    HeaderFlag() { _flag._header_flag = 0; }
    HeaderFlag(const HeaderFlag& flag) { _flag._header_flag = flag._flag._header_flag; }
    ~HeaderFlag() {}

    bool Encode(std::shared_ptr<IBufferWriteOnly> buffer);
    bool Decode(std::shared_ptr<IBufferReadOnly> buffer);
    uint32_t EncodeSize();

    uint8_t GetFlagInt() { return _flag._header_flag; }

    bool IsShortHeaderFlag() const;
    LongHeaderFlag GetLongHeaderFlag() const { return _flag._long_header_flag; }
    ShortHeaderFlag GetShortHeaderFlag() const { return _flag._short_header_flag; }

    HeaderFlag& operator=(HeaderFlag& flag) {
        _flag._header_flag = flag._flag._header_flag;
        return *this;
    }

protected:
    union HeaderFlagUnion {
        uint8_t _header_flag;
        LongHeaderFlag  _long_header_flag;
        ShortHeaderFlag _short_header_flag;
    } _flag;
};

}

#endif