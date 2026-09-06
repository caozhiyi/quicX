#include <vector>

#include "http3/qpack/huffman_encoder.h"
#include "http3/qpack/util.h"

namespace quicx {
namespace http3 {

bool QpackEncodePrefixedInteger(
    std::shared_ptr<common::IBuffer> buf, uint8_t prefix_bits, uint8_t first_byte_prefix_mask, uint64_t value) {
    if (prefix_bits == 0 || prefix_bits > 8) return false;
    uint8_t max_in_prefix = static_cast<uint8_t>((1u << prefix_bits) - 1u);
    uint8_t first = first_byte_prefix_mask;
    if (value < max_in_prefix) {
        first |= static_cast<uint8_t>(value);
        return buf->Write(&first, 1) == 1;
    }
    first |= max_in_prefix;
    if (buf->Write(&first, 1) != 1) return false;
    value -= max_in_prefix;
    // Write 7-bit continuation bytes
    while (value >= 128) {
        uint8_t b = static_cast<uint8_t>((value % 128) + 128);
        if (buf->Write(&b, 1) != 1) return false;
        value /= 128;
    }
    uint8_t last = static_cast<uint8_t>(value);
    return buf->Write(&last, 1) == 1;
}

bool QpackDecodePrefixedInteger(
    const std::shared_ptr<common::IBuffer> buf, uint8_t prefix_bits, uint8_t& first_byte, uint64_t& value) {
    if (prefix_bits == 0 || prefix_bits > 8) return false;
    if (buf->Read(&first_byte, 1) != 1) return false;
    return QpackDecodePrefixedIntegerFrom(buf, prefix_bits, first_byte, value);
}

bool QpackEncodeStringLiteral(const std::string& s, std::shared_ptr<common::IBuffer> buf, bool huffman) {
    if (huffman) {
        // The flag has to describe the bytes that follow. This previously set
        // the Huffman bit and then wrote the string uncompressed, with the
        // uncompressed length -- so a peer would run Huffman decoding over raw
        // ASCII and get garbage. No production caller passed huffman=true, which
        // is why it went unnoticed, but the parameter is public and defaulted.
        const std::vector<uint8_t> encoded = HuffmanEncoder::Instance().Encode(s);
        if (!QpackEncodePrefixedInteger(buf, 7, 0x80, static_cast<uint64_t>(encoded.size()))) return false;
        if (encoded.empty()) return true;
        return buf->Write(encoded.data(), static_cast<uint32_t>(encoded.size())) == encoded.size();
    }

    // length prefix with 7-bit prefix
    if (!QpackEncodePrefixedInteger(buf, 7, 0x00, static_cast<uint64_t>(s.size()))) return false;
    if (s.empty()) return true;
    return buf->Write(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(s.data())),
               static_cast<uint32_t>(s.size())) == static_cast<uint32_t>(s.size());
}

// Continues a prefixed integer whose first byte the caller already consumed.
// The continuation run is peer-controlled and RFC 9204 §4.1.1 puts no cap on
// its length, so every step has to be checked:
//   - m >= 64 would make `<< m` undefined behaviour. Reachable with 10
//     continuation bytes.
//   - even below that, the shift or the addition can carry the value
//     past 2^64 and wrap silently.
// A value that cannot be represented is a malformed encoding, so reject
// rather than truncate.
bool QpackDecodePrefixedIntegerFrom(
    const std::shared_ptr<common::IBuffer> buf, uint8_t prefix_bits, uint8_t first_byte, uint64_t& value) {
    if (prefix_bits == 0 || prefix_bits > 8) return false;
    uint8_t max_in_prefix = static_cast<uint8_t>((1u << prefix_bits) - 1u);
    value = first_byte & max_in_prefix;
    if (value < max_in_prefix) return true;
    uint64_t m = 0;
    uint8_t b = 0;
    do {
        if (buf->Read(&b, 1) != 1) return false;
        // Same overflow reasoning as QpackDecodePrefixedInteger().
        if (m >= 64) return false;
        const uint64_t chunk = static_cast<uint64_t>(b & 0x7f);
        if (chunk > (UINT64_MAX >> m)) return false;
        const uint64_t add = chunk << m;
        if (value > UINT64_MAX - add) return false;
        value += add;
        m += 7;
    } while (b & 0x80);
    return true;
}

// Shared tail of the string-literal decoders: reads |len| bytes and Huffman
// decodes them if needed.
static bool QpackReadStringBody(
    const std::shared_ptr<common::IBuffer> buf, uint64_t len, bool huffman, std::string& out) {
    if (len == 0) {
        out.clear();
        return true;
    }

    // |len| is whatever the peer wrote. Bound it by what is actually readable
    // before allocating: a 5-byte header could otherwise request gigabytes.
    if (len > static_cast<uint64_t>(buf->GetDataLength())) {
        return false;
    }

    if (!huffman) {
        out.resize(static_cast<size_t>(len));
        return buf->Read(reinterpret_cast<uint8_t*>(&out[0]), static_cast<uint32_t>(len)) == static_cast<uint32_t>(len);
    }
    std::vector<uint8_t> tmp;
    tmp.resize(static_cast<size_t>(len));
    if (buf->Read(tmp.data(), static_cast<uint32_t>(len)) != static_cast<uint32_t>(len)) return false;
    out = HuffmanEncoder::Instance().Decode(tmp);
    return true;
}

bool QpackEncodeStringLiteralWithPrefix(const std::string& s, std::shared_ptr<common::IBuffer> buf,
    uint8_t length_prefix_bits, uint8_t instruction_mask, uint8_t huffman_bit, bool huffman) {
    if (huffman) {
        const std::vector<uint8_t> encoded = HuffmanEncoder::Instance().Encode(s);
        if (!QpackEncodePrefixedInteger(buf, length_prefix_bits, static_cast<uint8_t>(instruction_mask | huffman_bit),
                static_cast<uint64_t>(encoded.size()))) {
            return false;
        }
        if (encoded.empty()) return true;
        return buf->Write(encoded.data(), static_cast<uint32_t>(encoded.size())) == encoded.size();
    }

    if (!QpackEncodePrefixedInteger(buf, length_prefix_bits, instruction_mask, static_cast<uint64_t>(s.size()))) {
        return false;
    }
    if (s.empty()) return true;
    return buf->Write(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(s.data())),
               static_cast<uint32_t>(s.size())) == static_cast<uint32_t>(s.size());
}

bool QpackDecodeStringLiteralWithPrefix(const std::shared_ptr<common::IBuffer> buf, uint8_t first_byte,
    uint8_t length_prefix_bits, uint8_t huffman_bit, std::string& out) {
    uint64_t len = 0;
    if (!QpackDecodePrefixedIntegerFrom(buf, length_prefix_bits, first_byte, len)) {
        return false;
    }
    return QpackReadStringBody(buf, len, (first_byte & huffman_bit) != 0, out);
}

bool QpackDecodeStringLiteral(const std::shared_ptr<common::IBuffer> buf, std::string& out) {
    uint8_t first = 0;
    uint64_t len = 0;
    if (!QpackDecodePrefixedInteger(buf, 7, first, len)) {
        return false;
    }
    bool huffman = (first & 0x80) != 0;
    if (len == 0) {
        out.clear();
        return true;
    }

    // |len| is whatever the peer wrote. Bound it by what is actually readable
    // before allocating: a 5-byte header could otherwise request gigabytes.
    //
    // This also closes a hole in the old success check, which compared a
    // uint32_t-truncated read count against an int32_t-truncated length. With
    // len = 0x100000000 both sides truncated to 0, so it read nothing, compared
    // 0 == 0, and reported success holding a 4 GiB string. After this check
    // len <= GetDataLength(), which is a uint32_t, so neither cast can truncate.
    if (len > static_cast<uint64_t>(buf->GetDataLength())) {
        return false;
    }

    if (!huffman) {
        out.resize(static_cast<size_t>(len));
        return buf->Read(reinterpret_cast<uint8_t*>(&out[0]), static_cast<uint32_t>(len)) == static_cast<uint32_t>(len);
    }
    // Huffman encoded: read into temp buffer then decode
    std::vector<uint8_t> tmp;
    tmp.resize(static_cast<size_t>(len));
    if (buf->Read(tmp.data(), static_cast<uint32_t>(len)) != static_cast<uint32_t>(len)) return false;
    out = HuffmanEncoder::Instance().Decode(tmp);
    return true;
}

}  // namespace http3
}  // namespace quicx
