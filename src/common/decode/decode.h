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
char* DecodeVarint(char* start, char* end, uint64_t& value);

// return the char offset pos
// get length of decode result by: return - first param
char* DecodeVarint(char* start, char* end, uint32_t& value);


char* FixedEncodeUint8(char *pos, uint8_t value);
char* FixedDecodeUint8(char *pos, char *end, uint8_t& out);

char* FixedEncodeUint16(char *pos, uint16_t value);
char* FixedDecodeUint16(char *pos, char *end, uint16_t& out);

char* FixedEncodeUint32(char *pos, uint32_t value);
char* FixedDecodeUint32(char *pos, char *end, uint32_t& out);

char* FixedEncodeUint64(char *pos, uint64_t value);
char* FixedDecodeUint64(char *pos, char *end, uint64_t& out);

}

#endif