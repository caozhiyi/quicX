#include <gtest/gtest.h>
#include "http3/qpack/util.h"
#include "common/buffer/buffer.h"
#include "http3/frame/qpack_decoder_frames.h"

using namespace quicx::http3;

TEST(QpackDecoderFramesTest, PrefixedIntegerEncodeDecode) {
    uint8_t bufmem[64] = {0}; 
    auto buf = std::make_shared<quicx::common::Buffer>(bufmem, sizeof(bufmem));
    
    // Test encoding and decoding a small value
    uint64_t original_value = 123;
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 8, 0, original_value));
    
    auto read_buf = buf->GetReadViewPtr();
    uint8_t first_byte = 0;
    uint64_t decoded_value = 0;
    ASSERT_TRUE(QpackDecodePrefixedInteger(read_buf, 8, first_byte, decoded_value));
    EXPECT_EQ(decoded_value, original_value);
}

TEST(QpackDecoderFramesTest, SectionAckEncodeDecode) {
    QpackSectionAckFrame f1; f1.Set(123, 456);
    uint8_t bufmem[64] = {0}; auto buf = std::make_shared<quicx::common::Buffer>(bufmem, sizeof(bufmem));
    ASSERT_TRUE(f1.Encode(buf));
    
    // Create a read view for decoding
    auto read_buf = buf->GetReadViewPtr();
    
    // Now decode
    QpackSectionAckFrame f2;
    ASSERT_TRUE(f2.Decode(read_buf));
    EXPECT_EQ(f2.GetStreamId(), 123u);
    EXPECT_EQ(f2.GetSectionNumber(), 456u);
}

TEST(QpackDecoderFramesTest, StreamCancellationEncodeDecode) {
    QpackStreamCancellationFrame f1; f1.Set(777, 888);
    uint8_t bufmem[64] = {0}; auto buf = std::make_shared<quicx::common::Buffer>(bufmem, sizeof(bufmem));
    ASSERT_TRUE(f1.Encode(buf));
    
    // Create a read view for decoding
    auto read_buf = buf->GetReadViewPtr();

    QpackStreamCancellationFrame f2;
    ASSERT_TRUE(f2.Decode(read_buf));
    EXPECT_EQ(f2.GetStreamId(), 777u);
    EXPECT_EQ(f2.GetSectionNumber(), 888u);
}

TEST(QpackDecoderFramesTest, InsertCountIncrementEncodeDecode) {
    QpackInsertCountIncrementFrame f1; f1.Set(9999);
    uint8_t bufmem[32] = {0}; auto buf = std::make_shared<quicx::common::Buffer>(bufmem, sizeof(bufmem));
    ASSERT_TRUE(f1.Encode(buf));
    
    // Create a read view for decoding
    auto read_buf = buf->GetReadViewPtr();

    QpackInsertCountIncrementFrame f2;
    ASSERT_TRUE(f2.Decode(read_buf));
    EXPECT_EQ(f2.GetDelta(), 9999u);
}


