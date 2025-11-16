#include <gtest/gtest.h>
#include "common/decode/decode.h"
#include "http3/frame/goaway_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

class GoAwayFrameTest : public testing::Test {
protected:
    void SetUp() override {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
        buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
        frame_ = std::make_shared<GoAwayFrame>();
    }

    std::shared_ptr<common::SingleBlockBuffer> buffer_;
    std::shared_ptr<GoAwayFrame> frame_;
};

TEST_F(GoAwayFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FrameType::kGoAway);
    
    uint64_t stream_id = 100;
    frame_->SetStreamId(stream_id);
    EXPECT_EQ(frame_->GetStreamId(), stream_id);
}

TEST_F(GoAwayFrameTest, EncodeAndDecode) {
    uint64_t stream_id = 100;
    frame_->SetStreamId(stream_id);

    // Encode
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<GoAwayFrame>();
    EXPECT_TRUE(decode_frame->Decode(buffer_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetStreamId(), stream_id);
}

TEST_F(GoAwayFrameTest, EvaluateSize) {
    frame_->SetStreamId(100);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload (stream_id)
    uint32_t payload_size = frame_->EvaluatePayloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

}  // namespace
}  // namespace http3
}  // namespace quicx 