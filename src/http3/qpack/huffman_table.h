#ifndef HTTP3_QPACK_HUFFMAN_TABLE
#define HTTP3_QPACK_HUFFMAN_TABLE

#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace quicx {
namespace http3 {


class HuffmanTable {
public:
    HuffmanTable();
    ~HuffmanTable() = default;
    HuffmanTable(const HuffmanTable&) = delete;
    HuffmanTable& operator=(const HuffmanTable&) = delete;

    // decode a huffman encoded string
    bool Decode(const std::vector<uint8_t>& input, std::string& output) const;

private:
    struct HuffmanNode {
        uint8_t  next;   // next state
        uint8_t  emit;   // emit symbol
        uint8_t  symbol; // symbol
        uint8_t  ending; // ending state
    };
    static HuffmanNode huffman_table_[256][16];

    bool DecodeBits(uint8_t& state, uint8_t& ending, uint8_t bits, std::string& output) const;

    // count the number of padding bits
    uint8_t CountPaddingBits(uint8_t last_byte) const;
    // validate padding bits
    bool ValidatePadding(uint8_t last_byte, uint8_t padding_bits) const;
};

} 
}

#endif
