#ifndef HTTP3_QPACK_HUFFMAN_ENCODER
#define HTTP3_QPACK_HUFFMAN_ENCODER

#include <string>
#include <vector>
#include <cstdint>
#include "common/util/singleton.h"
#include "http3/qpack/huffman_table.h"

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
    std::vector<uint8_t> Encode(const std::string& input);

    // Decode a QPACK Huffman encoded string
    // Returns the decoded string, or empty string if decoding fails
    std::string Decode(const std::vector<uint8_t>& input);
private:
    // Helper function to write bits to output
    // code: The code to write
    // num_bits: Number of bits in the code
    // current_byte: The current byte to write to
    // bits_left: Number of bits left in the current byte
    // output: The output vector to write to
    void WriteBits(uint32_t code, uint8_t num_bits, uint32_t& current_byte, uint8_t& bits_left, std::vector<uint8_t>& output);

private:
    // Huffman code vector entry
    struct HuffmanCode {
        uint32_t symbol;  // The symbol to encode
        uint32_t code;     // The huffman code bits
        uint8_t num_bits;  // Number of bits in the code
    };

    // Static Huffman code vector as defined in QPACK spec
    static const std::vector<HuffmanCode> huffman_vector_;

    // Huffman table
    HuffmanTable huffman_table_;


};

}
}

#endif
