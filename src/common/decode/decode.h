#ifndef COMMON_DECODE_DECODE
#define COMMON_DECODE_DECODE

#include <cstdint>
#include <cstring>

namespace quicx {

// return the uint8_t offset pos
// get length of encode result by: return - first param
uint8_t* EncodeVarint(uint8_t* dst, uint64_t value);
uint16_t GetEncodeVarintLength(uint64_t value);

// return the uint8_t offset pos
// get length of decode result by: return - first param
uint8_t* DecodeVarint(uint8_t* start, uint8_t* end, uint64_t& value);

// return the uint8_t offset pos
// get length of decode result by: return - first param
uint8_t* DecodeVarint(uint8_t* start, uint8_t* end, uint32_t& value);


uint8_t* FixedEncodeUint8(uint8_t *start, uint8_t value);
uint8_t* FixedDecodeUint8(uint8_t *start, uint8_t *end, uint8_t& out);

uint8_t* FixedEncodeUint16(uint8_t *start, uint16_t value);
uint8_t* FixedDecodeUint16(uint8_t *start, uint8_t *end, uint16_t& out);

uint8_t* FixedEncodeUint32(uint8_t *start, uint32_t value);
uint8_t* FixedDecodeUint32(uint8_t *start, uint8_t *end, uint32_t& out);

uint8_t* FixedEncodeUint64(uint8_t *start, uint64_t value);
uint8_t* FixedDecodeUint64(uint8_t *start, uint8_t *end, uint64_t& out);

uint8_t* EncodeBytes(uint8_t *start, uint8_t *end, uint8_t* in, uint32_t in_len);
uint8_t* DecodeBytesCopy(uint8_t *start, uint8_t *end, uint8_t*& out, uint32_t out_len);
uint8_t* DecodeBytesNoCopy(uint8_t *start, uint8_t *end, uint8_t*& out, uint32_t out_len);

}

#endif