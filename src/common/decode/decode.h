#ifndef COMMON_DECODE_DECODE
#define COMMON_DECODE_DECODE

#include <cstdint>
#include <cstring>

namespace quicx {

// return the char offset pos
// get length of encode result by: return - first param
char* EncodeVarint(char* dst, uint64_t value);
uint16_t GetEncodeVarintLength(uint64_t value);

// return the char offset pos
// get length of decode result by: return - first param
char* DecodeVarint(char* start, char* end, uint64_t& value);

// return the char offset pos
// get length of decode result by: return - first param
char* DecodeVarint(char* start, char* end, uint32_t& value);


char* FixedEncodeUint8(char *start, uint8_t value);
char* FixedDecodeUint8(char *start, char *end, uint8_t& out);

char* FixedEncodeUint16(char *start, uint16_t value);
char* FixedDecodeUint16(char *start, char *end, uint16_t& out);

char* FixedEncodeUint32(char *start, uint32_t value);
char* FixedDecodeUint32(char *start, char *end, uint32_t& out);

char* FixedEncodeUint64(char *start, uint64_t value);
char* FixedDecodeUint64(char *start, char *end, uint64_t& out);

char* EncodeBytes(char *start, char *end, const char* in, uint32_t in_len);
char* DecodeBytesCopy(char *start, char *end, char*& out, uint32_t out_len);
char* DecodeBytesNoCopy(char *start, char *end, char*& out, uint32_t out_len);

}

#endif