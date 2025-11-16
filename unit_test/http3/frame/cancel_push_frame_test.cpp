#include <gtest/gtest.h>
#include "common/decode/decode.h"
#include "http3/frame/cancel_push_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

class CancelPushFrameTest : public testing::Test {
protected:
    void SetUp() override {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
        buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
        frame_ = std::make_shared<CancelPushFrame>();
    }
    std::shared_ptr<common::SingleBlockBuffer> buffer_;
    std::shared_ptr<CancelPushFrame> frame_;
};

TEST_F(CancelPushFrameTest, EncodeAndDecode) {
    uint64_t push_id = 100;
    frame_->SetPushId(push_id);

    // Encode
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<CancelPushFrame>();
    EXPECT_TRUE(decode_frame->Decode(buffer_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetPushId(), push_id);
}

TEST_F(CancelPushFrameTest, EncodeSize) {
    uint32_t push_id = 100;
    frame_->SetPushId(push_id);

    // Calculate expected size:
    // 1. frame type: 2 bytes
    // 2. length field: varint size for push_id length
    // 3. push_id: varint size for value 100
    uint32_t push_id_size = common::GetEncodeVarintLength(push_id);
    uint32_t length_field_size = common::GetEncodeVarintLength(push_id_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + push_id_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

TEST_F(CancelPushFrameTest, EvaluateSize) {
    frame_->SetPushId(100);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload (push_id)
    uint32_t payload_size = frame_->EvaluatePayloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

}  // namespace
}  // namespace http3
}  // namespace quicx 