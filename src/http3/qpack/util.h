#ifndef HTTP3_QPACK_UTIL
#define HTTP3_QPACK_UTIL

#include <utility>
#include <string>
#include <cstdint>
#include <functional>

#include "common/buffer/if_buffer_read.h"
#include "common/buffer/if_buffer_write.h"

namespace quicx {
namespace http3 {

// Custom hash function for std::pair
struct pair_hash {
    template <class T1, class T2>
    std::size_t operator () (const std::pair<T1,T2> &pair) const {
        auto hash1 = std::hash<T1>{}(pair.first);
        auto hash2 = std::hash<T2>{}(pair.second);
        return hash1 ^ hash2;
    }
};

// QPACK/HPACK-style prefixed integer encoding (RFC 9204 §4.1)
// prefix_bits in [1,8]; prefix_field is the low prefix_bits of first byte.
bool QpackEncodePrefixedInteger(std::shared_ptr<common::IBufferWrite> buf, uint8_t prefix_bits, uint8_t first_byte_prefix_mask, uint64_t value);
bool QpackDecodePrefixedInteger(const std::shared_ptr<common::IBufferRead> buf, uint8_t prefix_bits, uint8_t& first_byte, uint64_t& value);

// QPACK string literal with Huffman flag in MSB of length prefix (prefix 7 bits)
bool QpackEncodeStringLiteral(const std::string& s, std::shared_ptr<common::IBufferWrite> buf, bool huffman = false);
bool QpackDecodeStringLiteral(const std::shared_ptr<common::IBufferRead> buf, std::string& out);

}
}

#endif
