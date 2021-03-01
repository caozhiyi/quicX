#include "decode.h"

namespace quicx {

const static uint64_t __max_decode = ((uint64_t)-1) >> 2;

enum EncodeLength {
    EL_1 = 0x00,
    EL_2 = 0x01,
    EL_4 = 0x02,
    EL_8 = 0x03,
    EL_MASK = ~(0x03 << 6),
};

// encode state machine
class EncodeState {
public:
    EncodeState() : _encode_length(EL_1), _loop_times(0) {}
    ~EncodeState() {}

    // return loop times
    uint32_t NextLoop() {
        uint32_t ret = 0;
        switch (_encode_length)
        {
        case EL_1:
            ret = 0;
            _encode_length = EL_2;
            break;
        
        case EL_2:
            ret = 2 - _loop_times++;
            if (_loop_times > 2) {
                _encode_length = EL_4;
            }
            
            break;

        case EL_4:
            ret = 4 - _loop_times++;
            if (_loop_times > 4) {
                _encode_length = EL_8;
            }
            break;
        
        case EL_8:
            ret = 8 - _loop_times++;
            break;
        }
        return ret;
    }

    uint8_t GetLength() {
        return (uint8_t)_encode_length;
    }

private:
    EncodeLength _encode_length;
    uint32_t _loop_times;
};

char* EncodeVarint(char* dst, uint64_t value) {
    if (value > __max_decode) {
        throw "too large decode number";
    }
    
    static const uint32_t max_num = 1 << 8;

    uint8_t* ptr = reinterpret_cast<uint8_t*>(dst);
    uint8_t* start = ptr;
    EncodeState es;
    uint32_t loop_times = 0;

    

    if (value >> 6 > 0) {
        loop_times = es.NextLoop();
    }


    while (loop_times > 0) {
        *(ptr++) = value;
        value >>= 8;
    }
    
    

    while (value >= max_num) {
        max_num = after_byte;
        
        length += 1;
    }
    *(ptr++) = static_cast<uint8_t>(value);
    uint32_t test = Align(length);
    *(start) |= Align(length) << 6;
    return reinterpret_cast<char*>(ptr);
}

char* DecodeVirint(char* start, char* end, uint64_t& value) {
    value = 0;

    uint8_t prefix = *(reinterpret_cast<const uint8_t*>(start++));
    int32_t length = prefix >> 6;
    length = 1 << length;
    
    uint32_t test = (uint32_t)prefix;
    value = prefix & __decode_length_mask;
    length--;

    while (length > 0) {
        uint32_t byte = *(reinterpret_cast<const uint8_t*>(start++));
        value |= byte << 8;
        length--;
    }
    return start;
}

}