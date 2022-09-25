#include <arpa/inet.h>

#include "common/log/log.h"
#include "common/decode/decode.h"

namespace quicx {

const static uint64_t __max_decode = ((uint64_t)-1) >> 2;

#define IntSet(p, value, len, bits)          \
    (*(p)++ = ((value >> ((len) * 8)) & 0xff) | ((bits) << 6))

uint8_t* EncodeVarint(uint8_t* dst, uint64_t value) {
    if (value > __max_decode) {
        LOG_ERROR("too large decode number");
        return dst;
    }

    uint8_t* p = dst;
    if (value < (1 << 6)) {
        IntSet(p, value, 0, 0);

    } else if (value < (1 << 14)) {
        IntSet(p, value, 1, 1);
        IntSet(p, value, 0, 0);

    } else if (value < (1 << 30)) {
        IntSet(p, value, 3, 2);
        IntSet(p, value, 2, 0);
        IntSet(p, value, 1, 0);
        IntSet(p, value, 0, 0);

    } else {
        IntSet(p, value, 7, 3);
        IntSet(p, value, 6, 0);
        IntSet(p, value, 5, 0);
        IntSet(p, value, 4, 0);
        IntSet(p, value, 3, 0);
        IntSet(p, value, 2, 0);
        IntSet(p, value, 1, 0);
        IntSet(p, value, 0, 0);
    }

    return p;
}

uint16_t GetEncodeVarintLength(uint64_t value) {
    if (value < (1 << 6)) {
        return 1;
    }

    if (value < (1 << 14)) {
        return 2;
    }

    if (value < (1 << 30)) {
        return 4;
    }

    return 8;
}

uint8_t* DecodeVarint(uint8_t* start, uint8_t* end, uint64_t& value) {
    if (start >= end) {
        return NULL;
    }

    uint8_t* uend = (uint8_t*)start;
    uint8_t* p = (uint8_t*)start;
    uint8_t len = 1 << (*p >> 6);

    value = *p++ & 0x3f;

    if ((size_t)(uend - p) < (len - 1)) {
        return NULL;
    }

    while (--len) {
        value = (value << 8) + *p++;
    }

    return (uint8_t*)p;
}

uint8_t* DecodeVarint(uint8_t* start, uint8_t* end, uint32_t& value) {
    uint64_t ret = 0;
    uint8_t* ret_pos = DecodeVarint(start, end, ret);
    value = uint32_t(ret);
    return ret_pos;
}

uint8_t* FixedEncodeUint8(uint8_t *start, uint8_t value) {
    *start++ = *(uint8_t*)&value;
    return start;
}

uint8_t* FixedDecodeUint8(uint8_t *start, uint8_t *end, uint8_t& out) {
    out = *(uint8_t*)start++;
    return start;
}

uint8_t* FixedEncodeUint16(uint8_t *start, uint16_t value) {
    *(uint16_t*)start = htons(value);
    return start + sizeof(uint16_t);
}

uint8_t* FixedDecodeUint16(uint8_t *start, uint8_t *end, uint16_t& out) {
    out = ntohs(*(uint16_t*)start);
    return start + sizeof(uint16_t);
}

uint8_t* FixedEncodeUint32(uint8_t *start, uint32_t value) {
    *(uint32_t*)start = htonl(value);
    return start + sizeof(uint32_t);
}

uint8_t* FixedDecodeUint32(uint8_t *start, uint8_t *end, uint32_t& out) {
    out = ntohl(*(uint32_t*)start);
    return start + sizeof(uint32_t);
}

uint8_t* FixedEncodeUint64(uint8_t *start, uint64_t value) {
    *(uint64_t*)start = htonl(value);
    return start + sizeof(uint64_t);
}

uint8_t* FixedDecodeUint64(uint8_t *start, uint8_t *end, uint64_t& out) {
    out = ntohl(*(uint64_t*)start);
    return start + sizeof(uint64_t);
}

uint8_t* EncodeBytes(uint8_t *start, uint8_t *end, const uint8_t* in, uint32_t in_len) {
    if (end - start < in_len) {
        LOG_ERROR("too small to encode bytes");
        return start;
    }

    memcpy(start, in, in_len);
    return start + in_len;
}

uint8_t* DecodeBytesCopy(uint8_t *start, uint8_t *end, uint8_t*& out, uint32_t out_len) {
    if (end - start < out_len) {
        LOG_ERROR("too small to decode bytes");
        return start;
    }

    memcpy(out, start, out_len);
    return start + out_len;
}

uint8_t* DecodeBytesNoCopy(uint8_t *start, uint8_t *end, uint8_t*& out, uint32_t out_len) {
    if (end - start < out_len) {
        LOG_ERROR("too small to decode bytes");
        return start;
    }

    out = start;
    return start + out_len;
}


}