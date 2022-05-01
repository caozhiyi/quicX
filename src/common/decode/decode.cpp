#include "decode.h"
#include <arpa/inet.h>

namespace quicx {

const static uint64_t __max_decode = ((uint64_t)-1) >> 2;

#define IntSet(p, value, len, bits)          \
    (*(p)++ = ((value >> ((len) * 8)) & 0xff) | ((bits) << 6))

char* EncodeVarint(char* dst, uint64_t value) {
    if (value > __max_decode) {
        throw "too large decode number";
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

char* FixedEncodeUint8(char *pos, uint8_t value) {
    *pos++ = *(char*)&value;
    return pos;
}

char* FixedDecodeUint8(char *pos, char *end, uint8_t& out) {
    out = *(uint8_t*)pos++;
    return pos;
}

char* FixedEncodeUint16(char *pos, uint16_t value) {
    *(uint16_t*)pos = htons(value);
    return pos + sizeof(uint16_t);
}

char* FixedDecodeUint16(char *pos, char *end, uint16_t& out) {
    out = ntohs(*(uint16_t*)pos);
    return pos + sizeof(uint16_t);
}

char* FixedEncodeUint32(char *pos, uint32_t value) {
    *(uint32_t*)pos = htonl(value);
    return pos + sizeof(uint32_t);
}

char* FixedDecodeUint32(char *pos, char *end, uint32_t& out) {
    out = ntohl(*(uint32_t*)pos);
    return pos + sizeof(uint32_t);
}

char* FixedEncodeUint64(char *pos, uint64_t value) {
    *(uint64_t*)pos = htonl(value);
    return pos + sizeof(uint64_t);
}

char* FixedDecodeUint64(char *pos, char *end, uint64_t& out) {
    out = ntohl(*(uint64_t*)pos);
    return pos + sizeof(uint64_t);
}

}