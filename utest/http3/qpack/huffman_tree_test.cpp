// huffman_tree_test.cpp
#include <gtest/gtest.h>
#include "http3/qpack/huffman_tree.h"

namespace quicx {
namespace http3 {
namespace {

class HuffmanTreeTest : public testing::Test {
protected:
    void SetUp() override {
        tree_ = std::make_unique<HuffmanTree>();
        // Insert test huffman codes
        tree_->Insert(0b0, 1, 'a');  // 'a' -> 0
        tree_->Insert(0b10, 2, 'b');  // 'b' -> 10
        tree_->Insert(0b11, 2, 'c');  // 'c' -> 11
    }

    std::unique_ptr<HuffmanTree> tree_;
};

TEST_F(HuffmanTreeTest, InsertAndFind) {
    uint8_t symbol;
    EXPECT_TRUE(tree_->Find(0b0, 1, symbol));
    EXPECT_EQ(symbol, 'a');

    EXPECT_TRUE(tree_->Find(0b10, 2, symbol));
    EXPECT_EQ(symbol, 'b');

    EXPECT_TRUE(tree_->Find(0b11, 2, symbol));
    EXPECT_EQ(symbol, 'c');

    // Test non-existent code
    EXPECT_FALSE(tree_->Find(0b111, 3, symbol));
}

TEST_F(HuffmanTreeTest, DecodeSimpleString) {
    // Encode: "abc" -> 0 10 11
    // In binary: 01100000
    std::vector<uint8_t> encoded = {0x60};  // 0110 0000
    
    std::string decoded;
    EXPECT_TRUE(tree_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, "abc");
}

TEST_F(HuffmanTreeTest, DecodeWithPadding) {
    // Encode: "ab" -> 0 10 + padding(1111)
    // In binary: 01011111
    std::vector<uint8_t> encoded = {0x5F};  // 0101 1111
    
    std::string decoded;
    EXPECT_TRUE(tree_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, "ab");
}

TEST_F(HuffmanTreeTest, DecodeEmptyInput) {
    std::vector<uint8_t> encoded;
    std::string decoded;
    EXPECT_TRUE(tree_->Decode(encoded, decoded));
    EXPECT_TRUE(decoded.empty());
}

TEST_F(HuffmanTreeTest, DecodeInvalidInput) {
    // Invalid code pattern
    std::vector<uint8_t> encoded = {0x80};  // 1000 0000
    std::string decoded;
    EXPECT_FALSE(tree_->Decode(encoded, decoded));
}

TEST_F(HuffmanTreeTest, DecodeInvalidPadding) {
    // Invalid padding (not all ones)
    std::vector<uint8_t> encoded = {0x50};  // 0101 0000
    std::string decoded;
    EXPECT_FALSE(tree_->Decode(encoded, decoded));
}

TEST_F(HuffmanTreeTest, DecodeLongString) {
    // Create a new tree with more realistic Huffman codes
    auto long_tree = std::make_unique<HuffmanTree>();
    long_tree->Insert(0b010, 3, 'e');
    long_tree->Insert(0b011, 3, 't');
    long_tree->Insert(0b100, 3, 'a');
    long_tree->Insert(0b101, 3, 'o');
    
    // Encode: "eta" -> 010 011 100
    // In binary: 01001110 0...
    std::vector<uint8_t> encoded = {0x4E, 0x80};
    
    std::string decoded;
    EXPECT_TRUE(long_tree->Decode(encoded, decoded));
    EXPECT_EQ(decoded, "eta");
}

TEST_F(HuffmanTreeTest, DecodeMultipleBytes) {
    // Create a tree with longer codes
    auto multi_tree = std::make_unique<HuffmanTree>();
    multi_tree->Insert(0b00000, 5, 'x');
    multi_tree->Insert(0b00001, 5, 'y');
    multi_tree->Insert(0b00010, 5, 'z');
    
    // Encode: "xyz" -> 00000 00001 00010
    // In binary: 00000000 01000101 11...
    std::vector<uint8_t> encoded = {0x00, 0x45, 0xE0};
    
    std::string decoded;
    EXPECT_TRUE(multi_tree->Decode(encoded, decoded));
    EXPECT_EQ(decoded, "xyz");
}

}  // namespace
}  // namespace http3
}  // namespace quicx