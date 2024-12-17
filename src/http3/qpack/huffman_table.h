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
        uint8_t  next;
        uint8_t  emit;
        uint8_t  symbol;
        uint8_t  ending;
    };
    static HuffmanNode huffman_table_[256][16];

    bool DecodeBits(uint8_t& state, uint8_t& ending, uint8_t bits, std::string& output) const;
};

} 
}

#endif
