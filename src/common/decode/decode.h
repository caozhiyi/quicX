#ifndef COMMON_DECODE_DECODE
#define COMMON_DECODE_DECODE

#include <cstdint>
#include <cstring>

namespace quicx {

// return the char offset pos
// get length of encode result by: return - first param
char* EncodeVarint(char* dst, uint64_t value);

// return the char offset pos
// get length of decode result by: return - first param
char* DecodeVirint(char* start, char* end, uint64_t& value);

}

#endif