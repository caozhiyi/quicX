#include <gtest/gtest.h>
#include "http3/qpack/huffman_table.h"
#include "http3/qpack/huffman_encoder.h"

namespace quicx {
namespace http3 {
namespace {

class HuffmanTableTest : public testing::Test {
protected:
    void SetUp() override {
        table_ = std::make_unique<HuffmanTable>();
        encoder_ = std::make_unique<HuffmanEncoder>();
    }

    std::unique_ptr<HuffmanEncoder> encoder_;
    std::unique_ptr<HuffmanTable> table_;
};

// Test empty input
TEST_F(HuffmanTableTest, DecodeEmptyInput) {
    std::vector<uint8_t> encoded;
    std::string decoded;
    EXPECT_TRUE(table_->Decode(encoded, decoded));
    EXPECT_TRUE(decoded.empty());
}

// Test decoding single character
TEST_F(HuffmanTableTest, DecodeSingleCharacter) {
    std::string input = "a";
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
    
    std::string decoded;
    EXPECT_TRUE(table_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, "a");
}

// Test decoding multiple characters
TEST_F(HuffmanTableTest, DecodeMultipleCharacters) {
    std::string input = "abc";
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
    
    std::string decoded;
    EXPECT_TRUE(table_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, "abc");
}

// Test decoding with padding bits
TEST_F(HuffmanTableTest, DecodeWithPadding) {
    std::string input = "ab";
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
    
    std::string decoded;
    EXPECT_TRUE(table_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, "ab");
}

// Test invalid input
TEST_F(HuffmanTableTest, DecodeInvalidInput) {
    // Invalid Huffman code
    std::vector<uint8_t> encoded = {0xFF};  // 11111111
    std::string decoded;
    
    EXPECT_FALSE(table_->Decode(encoded, decoded));
}

// Test invalid padding
TEST_F(HuffmanTableTest, DecodeInvalidPadding) {
    // Using 0's for padding (should be 1's)
    std::vector<uint8_t> encoded = {0x0C, 0x30};  // 00001100 00110000
    std::string decoded;
    
    EXPECT_FALSE(table_->Decode(encoded, decoded));
}

// Test common HTTP header strings
TEST_F(HuffmanTableTest, DecodeHttpHeaders) {
    std::string input = ":method";
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
    
    std::string decoded;
    EXPECT_TRUE(table_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, ":method");
}

// Test long string
TEST_F(HuffmanTableTest, DecodeLongString) {
    std::string input;
    for (size_t i = 0; i < 1000; i++) {
        input.push_back('a');
    }
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
    
    std::string decoded;
    EXPECT_TRUE(table_->Decode(encoded, decoded));
    EXPECT_EQ(decoded.length(), 1000);  // Each byte decodes to two 'a's
    EXPECT_EQ(decoded, std::string(1000, 'a'));
}

}  // namespace
}  // namespace http3
}  // namespace quicx 