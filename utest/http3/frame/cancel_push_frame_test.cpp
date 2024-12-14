#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/cancel_push_frame.h"
#include "common/buffer/buffer_decode_wrapper.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace http3 {
namespace {

class CancelPushFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>();
        write_wrapper_ = std::make_shared<common::BufferWrite>(buffer_);
        read_wrapper_ = std::make_shared<common::BufferRead>(buffer_);
        frame_ = std::make_shared<CancelPushFrame>();
    }

    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::BufferWrite> write_wrapper_;
    std::shared_ptr<common::BufferRead> read_wrapper_;
    std::shared_ptr<CancelPushFrame> frame_;
};

TEST_F(CancelPushFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FT_CANCEL_PUSH);
    
    uint64_t push_id = 100;
    frame_->SetPushId(push_id);
    EXPECT_EQ(frame_->GetPushId(), push_id);
}

TEST_F(CancelPushFrameTest, EncodeAndDecode) {
    uint64_t push_id = 100;
    frame_->SetPushId(push_id);

    // Encode
    EXPECT_TRUE(frame_->Encode(write_wrapper_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<CancelPushFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_wrapper_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetPushId(), push_id);
}

TEST_F(CancelPushFrameTest, EvaluateSize) {
    frame_->SetPushId(100);

    uint32_t expected_size = frame_->EvaluateEncodeSize();
    EXPECT_EQ(expected_size, frame_->EvaluatePaloadSize() + 1); // +1 for frame type
}

}  // namespace
}  // namespace http3
}  // namespace quicx 