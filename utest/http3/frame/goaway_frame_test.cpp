#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/goaway_frame.h"
#include "common/buffer/buffer_wrapper.h"

namespace quicx {
namespace http3 {
namespace {

class GoAwayFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>();
        write_wrapper_ = std::make_shared<common::BufferWrite>(buffer_);
        read_wrapper_ = std::make_shared<common::BufferRead>(buffer_);
        frame_ = std::make_shared<GoAwayFrame>();
    }

    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::BufferWrite> write_wrapper_;
    std::shared_ptr<common::BufferRead> read_wrapper_;
    std::shared_ptr<GoAwayFrame> frame_;
};

TEST_F(GoAwayFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FT_GOAWAY);
    
    uint64_t stream_id = 100;
    frame_->SetStreamId(stream_id);
    EXPECT_EQ(frame_->GetStreamId(), stream_id);
}

TEST_F(GoAwayFrameTest, EncodeAndDecode) {
    uint64_t stream_id = 100;
    frame_->SetStreamId(stream_id);

    // Encode
    EXPECT_TRUE(frame_->Encode(write_wrapper_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<GoAwayFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_wrapper_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetStreamId(), stream_id);
}

TEST_F(GoAwayFrameTest, EvaluateSize) {
    frame_->SetStreamId(100);

    uint32_t expected_size = frame_->EvaluateEncodeSize();
    EXPECT_EQ(expected_size, frame_->EvaluatePaloadSize() + 1); // +1 for frame type
}

}  // namespace
}  // namespace http3
}  // namespace quicx 