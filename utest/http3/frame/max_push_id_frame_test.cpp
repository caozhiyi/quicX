#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "common/decode/decode.h"
#include "http3/frame/max_push_id_frame.h"

namespace quicx {
namespace http3 {
namespace {

class MaxPushIdFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
        frame_ = std::make_shared<MaxPushIdFrame>();
    }

    uint8_t buf_[1024];
    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<MaxPushIdFrame> frame_;
};

TEST_F(MaxPushIdFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), static_cast<uint16_t>(FrameType::kMaxPushId));
    
    uint64_t push_id = 100;
    frame_->SetPushId(push_id);
    EXPECT_EQ(frame_->GetPushId(), push_id);
}

TEST_F(MaxPushIdFrameTest, EncodeAndDecode) {
    uint64_t push_id = 100;
    frame_->SetPushId(push_id);

    // Encode
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<MaxPushIdFrame>();
    EXPECT_TRUE(decode_frame->Decode(buffer_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetPushId(), push_id);
}

TEST_F(MaxPushIdFrameTest, EvaluateSize) {
    frame_->SetPushId(100);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload (push_id)
    uint32_t payload_size = frame_->EvaluatePaloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

}  // namespace
}  // namespace http3
}  // namespace quicx 