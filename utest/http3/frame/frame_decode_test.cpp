#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/frame/settings_frame.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/cancel_push_frame.h"
#include "http3/frame/push_promise_frame.h"
#include "common/buffer/buffer_encode_wrapper.h"

namespace quicx {
namespace http3 {
namespace {

class FrameDecodeTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
        write_buffer_ = buffer_->GetWriteViewPtr();
        read_buffer_ = buffer_->GetReadViewPtr();
    }
    uint8_t buf_[1024];
    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::IBufferWrite> write_buffer_;
    std::shared_ptr<common::IBufferRead> read_buffer_;
};

TEST_F(FrameDecodeTest, DecodeDataFrame) {
    common::BufferEncodeWrapper write_wrapper(write_buffer_);
    // Write DATA frame type
    write_wrapper.EncodeFixedUint8(FT_DATA);
    // Write length
    write_wrapper.EncodeVarint(5);
    // Write payload
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    write_wrapper.EncodeBytes(data.data(), data.size());
    write_wrapper.Flush();

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FT_DATA);

    auto data_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    EXPECT_NE(data_frame, nullptr);
    EXPECT_EQ(data_frame->GetData(), data);
}

TEST_F(FrameDecodeTest, DecodeHeadersFrame) {
    common::BufferEncodeWrapper write_wrapper(write_buffer_);

    write_wrapper.EncodeFixedUint8(FT_HEADERS);
    write_wrapper.EncodeVarint(5);
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    write_wrapper.EncodeBytes(fields.data(), fields.size());

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FT_HEADERS);

    auto headers_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    EXPECT_NE(headers_frame, nullptr);
    EXPECT_EQ(headers_frame->GetEncodedFields(), fields);
}

TEST_F(FrameDecodeTest, DecodeSettingsFrame) {
    common::BufferEncodeWrapper write_wrapper(write_buffer_);
    write_wrapper.EncodeFixedUint8(FT_SETTINGS);
    // Write two settings
    write_wrapper.EncodeVarint(2);  // number of settings
    write_wrapper.EncodeVarint(1);  // setting id
    write_wrapper.EncodeVarint(100);  // setting value
    write_wrapper.EncodeVarint(2);  // setting id
    write_wrapper.EncodeVarint(200);  // setting value

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FT_SETTINGS);

    auto settings_frame = std::dynamic_pointer_cast<SettingsFrame>(frame);
    EXPECT_NE(settings_frame, nullptr);
    uint64_t value;
    EXPECT_TRUE(settings_frame->GetSetting(1, value));
    EXPECT_EQ(value, 100);
    EXPECT_TRUE(settings_frame->GetSetting(2, value));
    EXPECT_EQ(value, 200);
}

TEST_F(FrameDecodeTest, DecodeGoAwayFrame) {
    common::BufferEncodeWrapper write_wrapper(write_buffer_);
    write_wrapper.EncodeFixedUint8(FT_GOAWAY);
    write_wrapper.EncodeVarint(100);  // stream id

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FT_GOAWAY);

    auto goaway_frame = std::dynamic_pointer_cast<GoAwayFrame>(frame);
    EXPECT_NE(goaway_frame, nullptr);
    EXPECT_EQ(goaway_frame->GetStreamId(), 100);
}

TEST_F(FrameDecodeTest, DecodePushPromiseFrame) {
    common::BufferEncodeWrapper write_wrapper(write_buffer_);
    write_wrapper.EncodeFixedUint8(FT_PUSH_PROMISE);
    write_wrapper.EncodeVarint(100);  // push id
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    write_wrapper.EncodeBytes(fields.data(), fields.size());

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FT_PUSH_PROMISE);

    auto push_promise_frame = std::dynamic_pointer_cast<PushPromiseFrame>(frame);
    EXPECT_NE(push_promise_frame, nullptr);
    EXPECT_EQ(push_promise_frame->GetPushId(), 100);
    EXPECT_EQ(push_promise_frame->GetEncodedFields(), fields);
}

TEST_F(FrameDecodeTest, DecodeInvalidFrameType) {
    // Write invalid frame type
    common::BufferEncodeWrapper write_wrapper(write_buffer_);
    write_wrapper.EncodeFixedUint8(0xFF);

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_FALSE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeEmptyBuffer) {
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_FALSE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeIncompleteFrame) {
    // Write only frame type without payload
    common::BufferEncodeWrapper write_wrapper(write_buffer_);
    write_wrapper.EncodeFixedUint8(FT_DATA);

    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_FALSE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeMultipleFrames) {
    common::BufferEncodeWrapper write_wrapper(write_buffer_);
    // Write DATA frame
    write_wrapper.EncodeFixedUint8(FT_DATA);
    write_wrapper.EncodeVarint(3);
    std::vector<uint8_t> data = {1, 2, 3};
    write_wrapper.EncodeBytes(data.data(), data.size());

    // Write HEADERS frame
    write_wrapper.EncodeFixedUint8(FT_HEADERS);
    write_wrapper.EncodeVarint(2);
    std::vector<uint8_t> fields = {4, 5};
    write_wrapper.EncodeBytes(fields.data(), fields.size());

    // Decode first frame
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(DecodeFrames(read_buffer_, frames));
    EXPECT_EQ(frames.size(), 2);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FT_DATA);

    frame = frames[1];
    EXPECT_EQ(frame->GetType(), FT_HEADERS);
}

}  // namespace
}  // namespace http3
}  // namespace quicx 