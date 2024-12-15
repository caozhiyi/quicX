#ifndef HTTP3_QPACK_HUFFMAN_ENCODER
#define HTTP3_QPACK_HUFFMAN_ENCODER

#include <string>
#include <vector>
#include <cstdint>
#include "common/util/singleton.h"
#include "http3/qpack/huffman_tree.h"

namespace quicx {
namespace http3 {

class HuffmanEncoder:
    public common::Singleton<HuffmanEncoder> {
public:
    HuffmanEncoder();
    virtual ~HuffmanEncoder() = default;

    // Evaluate if Huffman encoding would be beneficial
    // Returns true if Huffman encoding would result in a smaller size
    bool ShouldHuffmanEncode(const std::string& input);

    // Encode a string using QPACK Huffman encoding
    // Returns the encoded string
    std::string Encode(const std::string& input);

    // Decode a QPACK Huffman encoded string
    // Returns the decoded string, or empty string if decoding fails
    std::string Decode(const std::string& input);
private:
    // Helper function to write bits to output
    void WriteBits(uint32_t bits, uint8_t num_bits, uint32_t& current_byte, uint8_t& bits_left, std::string& output);
private:
    // Huffman code table entry
    struct HuffmanCode {
        uint32_t symbol;  // The symbol to encode
        uint32_t code;     // The huffman code bits
        uint8_t num_bits;  // Number of bits in the code
    };

    // Static Huffman code table as defined in QPACK spec
    static const std::vector<HuffmanCode> huffman_table_;

    // Huffman tree
    HuffmanTree huffman_tree_;


};

}
}

#endif
