#ifndef HTTP3_QPACK_UTIL
#define HTTP3_QPACK_UTIL

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "common/buffer/if_buffer.h"

namespace quicx {
namespace http3 {

// Custom hash function for std::pair
struct pair_hash {
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2>& pair) const {
        auto hash1 = std::hash<T1>{}(pair.first);
        auto hash2 = std::hash<T2>{}(pair.second);
        return hash1 ^ hash2;
    }
};

// QPACK/HPACK-style prefixed integer encoding (RFC 9204 §4.1)
// prefix_bits in [1,8]; prefix_field is the low prefix_bits of first byte.
bool QpackEncodePrefixedInteger(
    std::shared_ptr<common::IBuffer> buf, uint8_t prefix_bits, uint8_t first_byte_prefix_mask, uint64_t value);
bool QpackDecodePrefixedInteger(
    const std::shared_ptr<common::IBuffer> buf, uint8_t prefix_bits, uint8_t& first_byte, uint64_t& value);

// QPACK string literal with Huffman flag in MSB of length prefix (prefix 7 bits)
bool QpackEncodeStringLiteral(const std::string& s, std::shared_ptr<common::IBuffer> buf, bool huffman = false);
bool QpackDecodeStringLiteral(const std::shared_ptr<common::IBuffer> buf, std::string& out);

// RFC 9204 §4.1.2: a string literal does not have to start on a byte boundary.
// "Insert with Literal Name" for instance packs the H flag and a 5-bit length
// into the same byte that carries the 01 instruction pattern, so the first byte
// cannot be consumed separately.
//   |instruction_mask| : fixed pattern bits of the first byte
//   |huffman_bit|      : mask of the H flag inside that same byte
//   |length_prefix_bits|: width of the length field below the H flag
bool QpackEncodeStringLiteralWithPrefix(const std::string& s, std::shared_ptr<common::IBuffer> buf,
    uint8_t length_prefix_bits, uint8_t instruction_mask, uint8_t huffman_bit, bool huffman = false);
// |first_byte| must already have been read from |buf| by the caller.
bool QpackDecodeStringLiteralWithPrefix(const std::shared_ptr<common::IBuffer> buf, uint8_t first_byte,
    uint8_t length_prefix_bits, uint8_t huffman_bit, std::string& out);

}  // namespace http3
}  // namespace quicx

#endif
