#ifndef COMMON_DECODE_DECODE
#define COMMON_DECODE_DECODE

#include <cstdint>
#include <cstring>
#include "os_info.h"

namespace quicx {

const static uint8_t __decode_length_mask = 0x03 << 6;
const static uint64_t __max_decode = 1 << 62;

// return the char offset pos
// get length of encode result by: return - first param
char* EncodeVarint(char* dst, uint64_t value) {
    if (value > __max_decode) {
        throw "too large decode number";
    }
    
    static const uint32_t B = 1 << 8;
    uint32_t length = 0;
    uint8_t* ptr = reinterpret_cast<uint8_t*>(dst);
    uint8_t* start = ptr;
    while (value >= B) {
        *(ptr++) = value;
        value >>= 8;
        length += 1;
    }
    *(ptr++) = static_cast<uint8_t>(value);
    *(start) |= length << 6;
    return reinterpret_cast<char*>(ptr);
}

// return the char offset pos
// get length of decode result by: return - first param
char* DecodeVirint(char* start, char* end, uint64_t& value) {
    uint64_t result = 0;

    uint8_t value = *(reinterpret_cast<const uint8_t*>(start));
    uint8_t length = value >> 6;
    
    result = value & __decode_length_mask;
    while (length > 0) {
        uint32_t byte = *(reinterpret_cast<const uint8_t*>(++start));
        result |= byte << 8;
        length--;
    }
    return start;
}

}

#endif