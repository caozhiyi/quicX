#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/push_promise_frame.h"

namespace quicx {
namespace http3 {
namespace {

class PushPromiseFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
        write_buffer_ = buffer_->GetWriteViewPtr();
        read_buffer_ = buffer_->GetReadViewPtr();
        frame_ = std::make_shared<PushPromiseFrame>();
    }

    uint8_t buf_[1024];
    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::IBufferWrite> write_buffer_;
    std::shared_ptr<common::IBufferRead> read_buffer_;
    std::shared_ptr<PushPromiseFrame> frame_;
};

TEST_F(PushPromiseFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FT_PUSH_PROMISE);
    
    uint64_t push_id = 100;
    frame_->SetPushId(push_id);
    EXPECT_EQ(frame_->GetPushId(), push_id);

    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    frame_->SetEncodedFields(fields);
    EXPECT_EQ(frame_->GetEncodedFields(), fields);
}

TEST_F(PushPromiseFrameTest, EncodeAndDecode) {
    uint64_t push_id = 100;
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    frame_->SetPushId(push_id);
    frame_->SetEncodedFields(fields);

    // Encode
    EXPECT_TRUE(frame_->Encode(write_buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<PushPromiseFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_buffer_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetPushId(), push_id);
    EXPECT_EQ(decode_frame->GetEncodedFields(), fields);
}

TEST_F(PushPromiseFrameTest, EvaluateSize) {
    frame_->SetPushId(100);
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    frame_->SetEncodedFields(fields);

    uint32_t expected_size = frame_->EvaluateEncodeSize();
    EXPECT_EQ(expected_size, frame_->EvaluatePaloadSize() + 1); // +1 for frame type
}

}  // namespace
}  // namespace http3
}  // namespace quicx 