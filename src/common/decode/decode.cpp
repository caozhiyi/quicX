#include <arpa/inet.h>

#include "common/log/log.h"
#include "common/decode/decode.h"

namespace quicx {

const static uint64_t __max_decode = ((uint64_t)-1) >> 2;

#define IntSet(p, value, len, bits)          \
    (*(p)++ = ((value >> ((len) * 8)) & 0xff) | ((bits) << 6))

char* EncodeVarint(char* dst, uint64_t value) {
    if (value > __max_decode) {
        LOG_ERROR("too large decode number");
        return dst;
    }

    char* p = dst;
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

char* DecodeVarint(char* start, char* end, uint64_t& value) {
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

    return (char*)p;
}

char* DecodeVarint(char* start, char* end, uint32_t& value) {
    uint64_t ret = 0;
    char* ret_pos = DecodeVarint(start, end, ret);
    value = uint32_t(ret);
    return ret_pos;
}

char* FixedEncodeUint8(char *start, uint8_t value) {
    *start++ = *(char*)&value;
    return start;
}

char* FixedDecodeUint8(char *start, char *end, uint8_t& out) {
    out = *(uint8_t*)start++;
    return start;
}

char* FixedEncodeUint16(char *start, uint16_t value) {
    *(uint16_t*)start = htons(value);
    return start + sizeof(uint16_t);
}

char* FixedDecodeUint16(char *start, char *end, uint16_t& out) {
    out = ntohs(*(uint16_t*)start);
    return start + sizeof(uint16_t);
}

char* FixedEncodeUint32(char *start, uint32_t value) {
    *(uint32_t*)start = htonl(value);
    return start + sizeof(uint32_t);
}

char* FixedDecodeUint32(char *start, char *end, uint32_t& out) {
    out = ntohl(*(uint32_t*)start);
    return start + sizeof(uint32_t);
}

char* FixedEncodeUint64(char *start, uint64_t value) {
    *(uint64_t*)start = htonl(value);
    return start + sizeof(uint64_t);
}

char* FixedDecodeUint64(char *start, char *end, uint64_t& out) {
    out = ntohl(*(uint64_t*)start);
    return start + sizeof(uint64_t);
}

char* EncodeBytes(char *start, char *end, const char* in, uint32_t in_len) {
    if (end - start < in_len) {
        LOG_ERROR("too small to encode bytes");
        return start;
    }

    memcpy(start, in, in_len);
    
    return start + in_len;
}

char* DecodeBytesCopy(char *start, char *end, char*& out, uint32_t out_len) {
    if (end - start < out_len) {
        LOG_ERROR("too small to decode bytes");
        return start;
    }

    memcpy(out, start, out_len);
    
    return start + out_len;
}

char* DecodeBytesNoCopy(char *start, char *end, char*& out, uint32_t out_len) {
    if (end - start < out_len) {
        LOG_ERROR("too small to decode bytes");
        return start;
    }

    out = start;
    
    return start + out_len;
}


}