#include <gtest/gtest.h>

// huffman_encoder_test.cpp
#include "http3/qpack/huffman_encoder.h"

namespace quicx {
namespace http3 {
namespace {

class HuffmanEncoderTest: public testing::Test {
protected:
    void SetUp() override { encoder_ = std::make_unique<HuffmanEncoder>(); }

    std::unique_ptr<HuffmanEncoder> encoder_;
};

TEST_F(HuffmanEncoderTest, EncodeEmptyString) {
    std::string input;
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_TRUE(encoded.empty());
}

TEST_F(HuffmanEncoderTest, EncodeSingleCharacter) {
    std::string input = "a";
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());

    std::string decoded;
    EXPECT_TRUE(encoder_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, input);
}

TEST_F(HuffmanEncoderTest, EncodeString) {
    std::string input = "test";
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());

    std::string decoded;
    EXPECT_TRUE(encoder_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, input);
}

TEST_F(HuffmanEncoderTest, EncodeSpecialCharacters) {
    std::string input = "!@#$%";
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());

    std::string decoded;
    EXPECT_TRUE(encoder_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, input);
}

TEST_F(HuffmanEncoderTest, EncodeLongString) {
    std::string input(1000, 'x');  // String with 1000 'x' characters
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    EXPECT_FALSE(encoded.empty());

    std::string decoded;
    EXPECT_TRUE(encoder_->Decode(encoded, decoded));
    EXPECT_EQ(decoded, input);
}

// P1-H5f: malformed Huffman bitstream must be reported as a decode failure
// (bool false), not silently decoded into an empty string — an empty value
// is legal on the wire, so the old empty-string sentinel turned garbage
// into a valid-looking header value.
TEST_F(HuffmanEncoderTest, MalformedBitstreamDecodesFalse) {
    std::string input = "test";
    std::vector<uint8_t> encoded = encoder_->Encode(input);
    ASSERT_FALSE(encoded.empty());

    // Corrupt the EOS padding bits (last byte's low bits must be all-1s for
    // a valid terminator; flipping them yields an invalid padding).
    encoded.back() ^= 0x01;

    std::string decoded;
    // A 4-char ASCII string may end on a byte boundary where the corruption
    // is inside the symbol stream instead of the padding — in that case the
    // table decode still fails. Either way the call must not report success
    // with garbage; assert the failure OR exact round-trip is impossible
    // after corruption, so simply require: failure, or a value that is not
    // the corrupted input silently accepted. Strictest useful assertion:
    // the decode either fails, or returns something (never silently OK on
    // a corrupted stream without detection).
    bool ok = encoder_->Decode(encoded, decoded);
    EXPECT_FALSE(ok);
}

}  // namespace
}  // namespace http3
}  // namespace quicx
