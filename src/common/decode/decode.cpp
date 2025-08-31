#ifdef _WIN32
// Windows headers must be included in the correct order
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define htobe64(x) htonll(x)
#define htole64(x) (x)
#define be64toh(x) ntohll(x)
#define le64toh(x) (x)
#endif

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)
#endif

#ifdef __linux__
#include <endian.h>
#include <arpa/inet.h>
#endif

#include "common/log/log.h"
#include "common/decode/decode.h"

namespace quicx {
namespace common {

const static uint64_t kMaxDecode = ((uint64_t)-1) >> 2;

#define IntSet(p, value, len, bits)          \
    (*(p)++ = (uint8_t)(((value >> ((len) * 8)) & 0xff) | ((bits) << 6)))

uint8_t* EncodeVarint(uint8_t* start, uint8_t* end, uint64_t value) {
    if (start >= end) {
        return nullptr;
    }

    uint16_t need_len = GetEncodeVarintLength(value);
    if (start + need_len > end) {
        return nullptr;
    }

    if (value > kMaxDecode) {
        LOG_ERROR("too large decode number:0x%lx", value);
        return nullptr;
    }

    uint8_t* p = start;
    if (value < (1ULL << 6)) {
        if (p + 1 > end) {
            return nullptr;
        }
        IntSet(p, value, 0, 0);

    } else if (value < (1ULL << 14)) {
        if (p + 2 > end) {
            return nullptr;
        }
        IntSet(p, value, 1, 1);
        IntSet(p, value, 0, 0);

    } else if (value < (1ULL << 30)) {
        if (p + 4 > end) {
            return nullptr;
        }
        IntSet(p, value, 3, 2);
        IntSet(p, value, 2, 0);
        IntSet(p, value, 1, 0);
        IntSet(p, value, 0, 0);

    } else {
        if (p + 8 > end) {
            return nullptr;
        }
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
    if (value <= 0x3F) {
        return 1;
    } else if (value <= 0x3FFF) {
        return 2; 
    } else if (value <= 0x3FFFFFFF) {
        return 4;
    }
    return 8;
}

uint8_t* DecodeVarint(uint8_t* start, uint8_t* end, uint64_t& value) {
    if (start == nullptr || end == nullptr || start >= end) {
        return nullptr;
    }

    // Correct upper bound pointer for range checks
    uint8_t* uend = end;
    uint8_t* p = start;
    uint8_t len = 1 << (*p >> 6);

    value = *p++ & 0x3f;

    if ((size_t)(uend - p) < (len - 1)) {
        return nullptr;
    }

    while (--len) {
        value = (value << 8) + *p++;
        if (p >= end) {
            (uint8_t*)p;
        }
    }

    return (uint8_t*)p;
}

uint8_t* DecodeVarint(uint8_t* start, uint8_t* end, uint32_t& value) {
    if (start == nullptr || end == nullptr || start >= end) {
        return nullptr;
    }

    uint64_t ret = 0;
    uint8_t* ret_pos = DecodeVarint(start, end, ret);
    value = uint32_t(ret);
    return ret_pos;
}

uint8_t* FixedEncodeUint8(uint8_t *start, uint8_t *end, uint8_t value) {
    if (start == nullptr || end == nullptr || start >= end) {
        return nullptr;
    }
    *start++ = value;
    return start;
}

uint8_t* FixedDecodeUint8(uint8_t *start, uint8_t *end, uint8_t& out) {
    if (start == nullptr || end == nullptr || start >= end) {
        return nullptr;
    }
    out = *start++;
    return start;
}

uint8_t* FixedEncodeUint16(uint8_t *start, uint8_t *end, uint16_t value) {
    if (start == nullptr || end == nullptr || start >= end) {
        return nullptr;
    }
    *(uint16_t*)start = htons(value);
    return start + sizeof(uint16_t);
}

uint8_t* FixedDecodeUint16(uint8_t *start, uint8_t *end, uint16_t& out) {
    if (start == nullptr || end == nullptr || start + sizeof(uint16_t) > end) {
        return nullptr;
    }
    out = ntohs(*(const uint16_t*)start);
    return start + sizeof(uint16_t);
}

uint8_t* FixedEncodeUint32(uint8_t *start, uint8_t *end, uint32_t value) {
    if (start == nullptr || end == nullptr || start + sizeof(uint32_t) > end) {
        return nullptr;
    }
    *(uint32_t*)start = htonl(value);
    return start + sizeof(uint32_t);
}

uint8_t* FixedDecodeUint32(uint8_t *start, uint8_t *end, uint32_t& out) {
    if (start == nullptr || end == nullptr || start + sizeof(uint32_t) > end) {
        return nullptr;
    }
    out = ntohl(*(uint32_t*)start);
    return start + sizeof(uint32_t);
}

uint8_t* FixedEncodeUint64(uint8_t *start, uint8_t *end, uint64_t value) {
    if (start == nullptr || end == nullptr || start + sizeof(uint64_t) > end) {
        return nullptr;
    }
    *(uint64_t*)start = htobe64(value);
    return start + sizeof(uint64_t);
}

uint8_t* FixedDecodeUint64(uint8_t *start, uint8_t *end, uint64_t& out) {
    if (start == nullptr || end == nullptr || start + sizeof(uint64_t) > end) {
        return nullptr;
    }
    out = be64toh(*(uint64_t*)start);
    return start + sizeof(uint64_t);
}

uint8_t* EncodeBytes(uint8_t *start, uint8_t *end, uint8_t* in, uint32_t in_len) {
    if (start == nullptr || end == nullptr || end - start < in_len) {
        LOG_ERROR("too small to encode bytes");
        return start;
    }

    memcpy(start, in, in_len);
    return start + in_len;
}

uint8_t* DecodeBytesCopy(uint8_t *start, uint8_t *end, uint8_t*& out, uint32_t out_len) {
    if (start == nullptr || end == nullptr || end - start < out_len) {
        LOG_ERROR("too small to decode bytes");
        return start;
    }

    memcpy(out, start, out_len);
    return start + out_len;
}

uint8_t* DecodeBytesNoCopy(uint8_t *start, uint8_t *end, uint8_t*& out, uint32_t out_len) {
    if (start == nullptr || end == nullptr || end - start < out_len) {
        LOG_ERROR("too small to decode bytes");
        return start;
    }

    out = start;
    return start + out_len;
}

}
}