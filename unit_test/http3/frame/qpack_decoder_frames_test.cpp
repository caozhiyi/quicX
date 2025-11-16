#include <gtest/gtest.h>
#include "http3/qpack/util.h"
#include "http3/frame/qpack_decoder_frames.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

TEST(QpackDecoderFramesTest, PrefixedIntegerEncodeDecode) {
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(64);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    
    // Test encoding and decoding a small value
    uint64_t original_value = 123;
    ASSERT_TRUE(QpackEncodePrefixedInteger(buf, 8, 0, original_value));
    
    uint8_t first_byte = 0;
    uint64_t decoded_value = 0;
    ASSERT_TRUE(QpackDecodePrefixedInteger(buf, 8, first_byte, decoded_value));
    EXPECT_EQ(decoded_value, original_value);
}

TEST(QpackDecoderFramesTest, SectionAckEncodeDecode) {
    QpackSectionAckFrame f1; f1.Set(123, 456);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(64);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    ASSERT_TRUE(f1.Encode(buf));

    // Now decode
    QpackSectionAckFrame f2;
    ASSERT_TRUE(f2.Decode(buf));
    EXPECT_EQ(f2.GetStreamId(), 123u);
    EXPECT_EQ(f2.GetSectionNumber(), 456u);
}

TEST(QpackDecoderFramesTest, StreamCancellationEncodeDecode) {
    QpackStreamCancellationFrame f1; f1.Set(777, 888);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(64);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    ASSERT_TRUE(f1.Encode(buf));
    
    QpackStreamCancellationFrame f2;
    ASSERT_TRUE(f2.Decode(buf));
    EXPECT_EQ(f2.GetStreamId(), 777u);
    EXPECT_EQ(f2.GetSectionNumber(), 888u);
}

TEST(QpackDecoderFramesTest, InsertCountIncrementEncodeDecode) {
    QpackInsertCountIncrementFrame f1; f1.Set(9999);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(32);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    ASSERT_TRUE(f1.Encode(buf));
    
    QpackInsertCountIncrementFrame f2;
    ASSERT_TRUE(f2.Decode(buf));
    EXPECT_EQ(f2.GetDelta(), 9999u);
}


}
}
}