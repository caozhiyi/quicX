#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/frame/push_promise_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace http3 {
namespace {

class FrameDecodeTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
    }
    uint8_t buf_[1024];
    std::shared_ptr<common::Buffer> buffer_;
};

TEST_F(FrameDecodeTest, DecodeDataFrame) {
    DataFrame data_frame;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    data_frame.SetData(data);
    data_frame.Encode(buffer_);

    auto read_buffer = buffer_->GetReadViewPtr();

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kData);

    auto decode_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    EXPECT_NE(decode_frame, nullptr);
    EXPECT_EQ(decode_frame->GetData(), data);
}

TEST_F(FrameDecodeTest, DecodeHeadersFrame) {
    HeadersFrame headers_frame;
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    headers_frame.SetEncodedFields(fields);
    headers_frame.Encode(buffer_);

    auto read_buffer = buffer_->GetReadViewPtr();

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kHeaders);

    auto decode_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    EXPECT_NE(decode_frame, nullptr);
    EXPECT_EQ(decode_frame->GetEncodedFields(), fields);
}

TEST_F(FrameDecodeTest, DecodeSettingsFrame) {
    SettingsFrame settings_frame;
    settings_frame.SetSetting(1, 100);
    settings_frame.SetSetting(2, 200);
    settings_frame.Encode(buffer_);
    
    auto read_buffer = buffer_->GetReadViewPtr();

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer, frames));
    ASSERT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kSettings);

    auto decode_frame = std::dynamic_pointer_cast<SettingsFrame>(frame);
    EXPECT_NE(decode_frame, nullptr);
}

TEST_F(FrameDecodeTest, DecodeGoAwayFrame) {
    GoAwayFrame goaway_frame;
    goaway_frame.SetStreamId(100);
    goaway_frame.Encode(buffer_);
    auto read_buffer = buffer_->GetReadViewPtr();

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer, frames));
    ASSERT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kGoAway);

    auto decode_frame = std::dynamic_pointer_cast<GoAwayFrame>(frame);
    EXPECT_NE(decode_frame, nullptr);
    EXPECT_EQ(decode_frame->GetStreamId(), 100);
}

TEST_F(FrameDecodeTest, DecodePushPromiseFrame) {
    PushPromiseFrame push_promise_frame;
    push_promise_frame.SetPushId(100);
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    push_promise_frame.SetEncodedFields(fields);
    push_promise_frame.Encode(buffer_);
    auto read_buffer = buffer_->GetReadViewPtr();

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer, frames));
    ASSERT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kPushPromise);

    auto decode_frame = std::dynamic_pointer_cast<PushPromiseFrame>(frame);
    EXPECT_NE(decode_frame, nullptr);
    EXPECT_EQ(decode_frame->GetPushId(), 100);
    EXPECT_EQ(decode_frame->GetEncodedFields(), fields);
}

TEST_F(FrameDecodeTest, DecodeInvalidFrameType) {
    // Write invalid frame type
    common::BufferEncodeWrapper write_wrapper(buffer_);
    write_wrapper.EncodeFixedUint8(0xFF);
    write_wrapper.Flush();
    auto read_buffer = buffer_->GetReadViewPtr();

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_FALSE(DecodeFrames(read_buffer, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeEmptyBuffer) {
    auto read_buffer = buffer_->GetReadViewPtr();
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_FALSE(DecodeFrames(read_buffer, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeIncompleteFrame) {
    // Write only frame type without payload
    common::BufferEncodeWrapper write_wrapper(buffer_);
    write_wrapper.EncodeFixedUint8(static_cast<uint8_t>(FrameType::kData));
    write_wrapper.Flush();
    auto read_buffer = buffer_->GetReadViewPtr();

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_FALSE(DecodeFrames(read_buffer, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeMultipleFrames) {
    DataFrame data_frame;
    std::vector<uint8_t> data = {1, 2, 3};
    data_frame.SetData(data);
    data_frame.Encode(buffer_);

    HeadersFrame headers_frame;
    std::vector<uint8_t> fields = {4, 5};
    headers_frame.SetEncodedFields(fields);
    headers_frame.Encode(buffer_);

    auto read_buffer = buffer_->GetReadViewPtr();

    // Decode first frame
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer, frames));
    ASSERT_EQ(frames.size(), 2);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kData);

    frame = frames[1];
    EXPECT_EQ(frame->GetType(), FrameType::kHeaders);
}

}  // namespace
}  // namespace http3
}  // namespace quicx 