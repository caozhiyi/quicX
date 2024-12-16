#include <cassert>
#include "http3/qpack/huffman_tree.h"

namespace quicx {
namespace http3 {

HuffmanTree::HuffmanTree():
    root_(std::make_unique<HuffmanNode>()) {

}

void HuffmanTree::Insert(uint32_t code, uint8_t num_bits, uint32_t symbol) {
    HuffmanNode* current = root_.get();
    
    // insert huffman code from high to low
    for (int i = num_bits - 1; i >= 0; i--) {
        int bit = (code >> i) & 1;
        
        if (!current->children[bit]) {
            current->children[bit] = std::make_unique<HuffmanNode>();
        }
        current = current->children[bit].get();
    }
    
    current->symbol = symbol;
    current->is_terminal = true;
}

bool HuffmanTree::Find(uint32_t code, uint8_t num_bits, uint32_t& symbol) const {
    const HuffmanNode* current = root_.get();
    
    // traverse the tree by huffman code
    for (int i = num_bits - 1; i >= 0; i--) {
        int bit = (code >> i) & 1;
        
        if (!current->children[bit]) {
            return false;
        }
        current = current->children[bit].get();
    }
    
    return DecodeSymbol(current, symbol);
}

bool HuffmanTree::Decode(const std::vector<uint8_t>& input, std::string& output) const {
    if (input.empty()) {
        output.clear();
        return true;
    }

    output.clear();
    const HuffmanNode* current = root_.get();
    uint8_t remaining_bits = 0;

    // process complete bytes
    for (size_t i = 0; i < input.size(); ++i) {
        uint8_t byte = input[i];
        remaining_bits = (i == input.size() - 1) ? CountPaddingBits(byte) : 0;

        for (int j = 7; j >= remaining_bits; --j) {
            int bit = (byte >> j) & 1;
            
            if (!current->children[bit]) {
                return false;  // invalid code
            }
            current = current->children[bit].get();

            uint32_t symbol;
            if (DecodeSymbol(current, symbol)) {
                if (symbol == EOS) {
                    return true;  // end of string
                }
                output.push_back(static_cast<char>(symbol));
                current = root_.get();  // reset to root node
            }
        }
    }

    // validate decoding is complete and legal
    return current == root_.get() && 
           ValidatePadding(input.back(), remaining_bits);
}

bool HuffmanTree::DecodeSymbol(const HuffmanNode* node, uint32_t& symbol) const {
    if (!node || !node->is_terminal) {
        return false;
    }
    symbol = node->symbol;
    return true;
}

uint8_t HuffmanTree::CountPaddingBits(uint8_t last_byte) const {
    uint8_t padding_bits = 0;
    uint8_t mask = 0x01;
    
    // check continuous 1 from lowest bit
    while ((last_byte & mask) == mask && padding_bits < 7) {
        padding_bits++;
        mask <<= 1;
    }
    
    return padding_bits;
}

bool HuffmanTree::ValidatePadding(uint8_t last_byte, uint8_t padding_bits) const {
    if (padding_bits == 0) {
        return true;
    }
    
    // check if padding bits are all 1
    uint8_t padding_mask = (1 << padding_bits) - 1;
    return (last_byte & padding_mask) == padding_mask;
}

}  // namespace http3
}  // namespace quicx
