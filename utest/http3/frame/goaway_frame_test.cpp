#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/goaway_frame.h"

namespace quicx {
namespace http3 {
namespace {

class GoAwayFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
        write_buffer_ = buffer_->GetWriteViewPtr();
        read_buffer_ = buffer_->GetReadViewPtr();
        frame_ = std::make_shared<GoAwayFrame>();
    }

    uint8_t buf_[1024];
    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::IBufferWrite> write_buffer_;
    std::shared_ptr<common::IBufferRead> read_buffer_;
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
    EXPECT_TRUE(frame_->Encode(write_buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<GoAwayFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_buffer_, true));

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