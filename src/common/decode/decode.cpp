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
        _loop_times++;
        switch (_encode_length)
        {
        case EL_1:
            _encode_length = EL_2;
        
        case EL_2:
            ret = 2 - _loop_times;
            if (_loop_times >= 2) {
                _encode_length = EL_4;
            } else {
                break;
            }

        case EL_4:
            ret = 4 - _loop_times;
            if (_loop_times >= 4) {
                _encode_length = EL_8;
            } else {
                break;
            }
        
        case EL_8:
            ret = 8 - _loop_times;
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

    uint8_t* ptr = reinterpret_cast<uint8_t*>(dst);
    uint8_t* start = ptr;
    EncodeState es;
    uint32_t loop_times = 0;

    bool first_byte = true;
    do
    {
        if (first_byte) {
            first_byte = false;
             // set first byte
            *(ptr++) = value & EL_MASK;
            uint32_t test = *(ptr - 1);

            value = value >> 6;
            if (value > 0) {
                loop_times = es.NextLoop();
            }

        } else {
            *(ptr++) = value;
            uint32_t test = *(ptr - 1);
            value >>= 8;
            if (value > 0) {
                loop_times = es.NextLoop();
            } else {
                loop_times--;
            }
        }
    } while (loop_times > 0);
    
    *(start) |= es.GetLength() << 6;
    return reinterpret_cast<char*>(ptr);
}

char* DecodeVirint(char* start, char* end, uint64_t& value) {
    const static uint8_t first_move = 6;
    const static uint8_t after_move = 8;

    value = 0;
    uint8_t prefix = *(reinterpret_cast<const uint8_t*>(start++));
    int32_t length = prefix >> 6;
    uint32_t loop_times = 1 << length;
    
    value = prefix & EL_MASK;
    loop_times--;

    uint8_t move = first_move;
    while (loop_times > 0) {
        uint64_t byte = *(reinterpret_cast<const uint8_t*>(start++));
        value |= byte << move;
        loop_times--;
        move += after_move;
   
    }
    return start;
}

}