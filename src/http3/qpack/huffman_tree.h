#ifndef HTTP3_QPACK_HUFFMAN_TREE
#define HTTP3_QPACK_HUFFMAN_TREE

#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace quicx {
namespace http3 {

class HuffmanTree {
private:
    struct HuffmanNode {
        uint8_t symbol;
        bool is_terminal;
        std::unique_ptr<HuffmanNode> children[2];

        HuffmanNode(): symbol(0), is_terminal(false) {
            children[0] = nullptr;
            children[1] = nullptr;
        }
    };

public:
    HuffmanTree();
    ~HuffmanTree() = default;
    HuffmanTree(const HuffmanTree&) = delete;
    HuffmanTree& operator=(const HuffmanTree&) = delete;

    // insert a huffman code
    void Insert(uint32_t code, uint8_t num_bits, uint8_t symbol);

    // find a symbol by code
    bool Find(uint32_t code, uint8_t num_bits, uint8_t& symbol) const;

    // decode a huffman encoded string
    bool Decode(const std::vector<uint8_t>& input, std::string& output) const;

private:
    std::unique_ptr<HuffmanNode> root_;
    static constexpr uint16_t EOS = 256;  // End of string marker
    
    // decode a symbol
    bool DecodeSymbol(const HuffmanNode* node, uint8_t& symbol) const;

    // count padding bits
    uint8_t CountPaddingBits(uint8_t last_byte) const;

    // validate padding
    bool ValidatePadding(uint8_t last_byte, uint8_t padding_bits) const;
};

} 
}

#endif
