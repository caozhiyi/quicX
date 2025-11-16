#include "http3/qpack/util.h"
#include "http3/qpack/huffman_encoder.h"

namespace quicx {
namespace http3 {

bool QpackEncodePrefixedInteger(std::shared_ptr<common::IBuffer> buf, uint8_t prefix_bits, uint8_t first_byte_prefix_mask, uint64_t value) {
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

bool QpackDecodePrefixedInteger(const std::shared_ptr<common::IBuffer> buf, uint8_t prefix_bits, uint8_t& first_byte, uint64_t& value) {
    if (prefix_bits == 0 || prefix_bits > 8) return false;
    if (buf->Read(&first_byte, 1) != 1) return false;
    uint8_t max_in_prefix = static_cast<uint8_t>((1u << prefix_bits) - 1u);
    value = first_byte & max_in_prefix;
    if (value < max_in_prefix) return true;
    uint64_t m = 0;
    uint8_t b = 0;
    do {
        if (buf->Read(&b, 1) != 1) return false;
        value += static_cast<uint64_t>(b & 0x7f) << m;
        m += 7;
    } while (b & 0x80);
    return true;
}

bool QpackEncodeStringLiteral(const std::string& s, std::shared_ptr<common::IBuffer> buf, bool huffman) {
    uint8_t h_bit = huffman ? 0x80 : 0x00;
    // length prefix with 7-bit prefix
    if (!QpackEncodePrefixedInteger(buf, 7, h_bit, static_cast<uint64_t>(s.size()))) return false;
    if (s.empty()) return true;
    return buf->Write(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(s.data())), static_cast<uint32_t>(s.size())) == static_cast<int32_t>(s.size());
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
    if (!huffman) {
        out.resize(static_cast<size_t>(len));
        return buf->Read(reinterpret_cast<uint8_t*>(&out[0]), static_cast<uint32_t>(len)) == static_cast<int32_t>(len);
    }
    // Huffman encoded: read into temp buffer then decode
    std::vector<uint8_t> tmp;
    tmp.resize(static_cast<size_t>(len));
    if (buf->Read(tmp.data(), static_cast<uint32_t>(len)) != static_cast<int32_t>(len)) return false;
    out = HuffmanEncoder::Instance().Decode(tmp);
    return true;
}

}
}


