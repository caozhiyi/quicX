// huffman_encoder_test.cpp
#include <gtest/gtest.h>
#include "http3/qpack/huffman_encoder.h"

namespace quicx {
namespace http3 {
namespace {

class HuffmanEncoderTest : public testing::Test {
protected:
    void SetUp() override {
        encoder_ = std::make_unique<HuffmanEncoder>();
    }

    std::unique_ptr<HuffmanEncoder> encoder_;
};

TEST_F(HuffmanEncoderTest, EncodeEmptyString) {
    std::string input;
    std::string encoded = encoder_->Encode(input);
    EXPECT_TRUE(encoded.empty());
}

TEST_F(HuffmanEncoderTest, EncodeSingleCharacter) {
    std::string input = "a";
    std::string encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
}

TEST_F(HuffmanEncoderTest, EncodeString) {
    std::string input = "test";
    std::string encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
}

TEST_F(HuffmanEncoderTest, EncodeSpecialCharacters) {
    std::string input = "!@#$%";
    std::string encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
}

TEST_F(HuffmanEncoderTest, EncodeLongString) {
    std::string input(1000, 'x');  // String with 1000 'x' characters
    std::string encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());
}

}  // namespace
}  // namespace http3
}  // namespace quicx