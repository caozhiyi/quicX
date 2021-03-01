#ifndef COMMON_DECODE_NORMAL_DECODE
#define COMMON_DECODE_NORMAL_DECODE

#include <cstdint>
#include <cstring>
#include "util/os_info.h"

namespace quicx {

// return the char offset pos
// get length of encode result by: return - first param
template<typename T>
char* EncodeVarint(char* dst, T value) {
    static const uint32_t B = 128;
    uint8_t* ptr = reinterpret_cast<uint8_t*>(dst);
    while (value >= B) {
        *(ptr++) = value | B;
        value >>= 7;
    }
    *(ptr++) = static_cast<uint8_t>(value);
    return reinterpret_cast<char*>(ptr);
}

// return the char offset pos
// get length of decode result by: return - first param
template<typename T>
char* DecodeVirint(char* start, char* end, T& value) {
    T result = 0;
    uint32_t bit_limit = (sizeof(T)-1) * sizeof(void*);
    for (uint32_t shift = 0; shift <= bit_limit && start < end; shift += 7) {
        T byte = *(reinterpret_cast<const uint8_t*>(start));
        start++;
        if (byte & 128) {
            // More bytes are present
            result |= ((byte & 127) << shift);
        } else {
            result |= (byte << shift);
            value = result;
            return start;
        }
    }
    return nullptr;
}

template<typename T>
char* EncodeFixed(char* dst, T value) {
    uint8_t* const buffer = reinterpret_cast<uint8_t*>(dst);

    if (!IsBigEndian()) {
        // Fast path for little-endian CPUs. All major compilers optimize this to a
        // single mov (x86_64) / str (ARM) instruction.
        std::memcpy(buffer, &value, sizeof(T));
        return dst + sizeof(T);
    }

    uint32_t bit_limit = sizeof(T) * sizeof(void*);
    for (size_t shift = 0, index = 0; shift <= bit_limit; shift+=8, index++) {
        buffer[index] = static_cast<uint8_t>(value >> shift);
    }
    return dst + sizeof(T);
}

template<typename T>
char* DecodeFixed(char* start, char* end, T& value) {
    const uint8_t* const buffer = reinterpret_cast<const uint8_t*>(start);

    if (!IsBigEndian()) {
        // Fast path for little-endian CPUs. All major compilers optimize this to a
        // single mov (x86_64) / ldr (ARM) instruction.
        std::memcpy(&value, buffer, sizeof(T));
        return start + sizeof(T);
    }

    uint32_t bit_limit = sizeof(T) * sizeof(void*);
    for (size_t shift = 0, index = 0; shift <= bit_limit; shift+=8, index++) {
        value |= (static_cast<T>(buffer[index]) << shift);
    }
    return start + sizeof(T);
}

}

#endif