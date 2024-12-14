#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decode.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/frame/settings_frame.h"
#include "common/buffer/buffer_wrapper.h"
#include "http3/frame/max_push_id_frame.h"
#include "http3/frame/cancel_push_frame.h"
#include "http3/frame/push_promise_frame.h"

namespace quicx {
namespace http3 {
namespace {

class FrameDecodeTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>();
        write_wrapper_ = std::make_shared<common::BufferWrite>(buffer_);
        read_wrapper_ = std::make_shared<common::BufferRead>(buffer_);
    }

    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::BufferWrite> write_wrapper_;
    std::shared_ptr<common::BufferRead> read_wrapper_;
};

TEST_F(FrameDecodeTest, DecodeDataFrame) {
    // Write DATA frame type
    write_wrapper_->WriteUint8(FT_DATA);
    // Write length
    write_wrapper_->WriteVarint(5);
    // Write payload
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    write_wrapper_->WriteBytes(data.data(), data.size());

    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(DecodeFrame(read_wrapper_, frame));
    EXPECT_EQ(frame->GetType(), FT_DATA);

    auto data_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    EXPECT_NE(data_frame, nullptr);
    EXPECT_EQ(data_frame->GetData(), data);
}

TEST_F(FrameDecodeTest, DecodeHeadersFrame) {
    write_wrapper_->WriteUint8(FT_HEADERS);
    write_wrapper_->WriteVarint(5);
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    write_wrapper_->WriteBytes(fields.data(), fields.size());

    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(DecodeFrame(read_wrapper_, frame));
    EXPECT_EQ(frame->GetType(), FT_HEADERS);

    auto headers_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    EXPECT_NE(headers_frame, nullptr);
    EXPECT_EQ(headers_frame->GetEncodedFields(), fields);
}

TEST_F(FrameDecodeTest, DecodeSettingsFrame) {
    write_wrapper_->WriteUint8(FT_SETTINGS);
    // Write two settings
    write_wrapper_->WriteVarint(2);  // number of settings
    write_wrapper_->WriteVarint(1);  // setting id
    write_wrapper_->WriteVarint(100);  // setting value
    write_wrapper_->WriteVarint(2);  // setting id
    write_wrapper_->WriteVarint(200);  // setting value

    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(DecodeFrame(read_wrapper_, frame));
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
    write_wrapper_->WriteUint8(FT_GOAWAY);
    write_wrapper_->WriteVarint(100);  // stream id

    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(DecodeFrame(read_wrapper_, frame));
    EXPECT_EQ(frame->GetType(), FT_GOAWAY);

    auto goaway_frame = std::dynamic_pointer_cast<GoAwayFrame>(frame);
    EXPECT_NE(goaway_frame, nullptr);
    EXPECT_EQ(goaway_frame->GetStreamId(), 100);
}

TEST_F(FrameDecodeTest, DecodePushPromiseFrame) {
    write_wrapper_->WriteUint8(FT_PUSH_PROMISE);
    write_wrapper_->WriteVarint(100);  // push id
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    write_wrapper_->WriteBytes(fields.data(), fields.size());

    std::shared_ptr<IFrame> frame;
    EXPECT_TRUE(DecodeFrame(read_wrapper_, frame));
    EXPECT_EQ(frame->GetType(), FT_PUSH_PROMISE);

    auto push_promise_frame = std::dynamic_pointer_cast<PushPromiseFrame>(frame);
    EXPECT_NE(push_promise_frame, nullptr);
    EXPECT_EQ(push_promise_frame->GetPushId(), 100);
    EXPECT_EQ(push_promise_frame->GetEncodedFields(), fields);
}

TEST_F(FrameDecodeTest, DecodeInvalidFrameType) {
    // Write invalid frame type
    write_wrapper_->WriteUint8(0xFF);

    std::shared_ptr<IFrame> frame;
    EXPECT_FALSE(DecodeFrame(read_wrapper_, frame));
    EXPECT_EQ(frame, nullptr);
}

TEST_F(FrameDecodeTest, DecodeEmptyBuffer) {
    std::shared_ptr<IFrame> frame;
    EXPECT_FALSE(DecodeFrame(read_wrapper_, frame));
    EXPECT_EQ(frame, nullptr);
}

TEST_F(FrameDecodeTest, DecodeIncompleteFrame) {
    // Write only frame type without payload
    write_wrapper_->WriteUint8(FT_DATA);

    std::shared_ptr<IFrame> frame;
    EXPECT_FALSE(DecodeFrame(read_wrapper_, frame));
}

TEST_F(FrameDecodeTest, DecodeMultipleFrames) {
    // Write DATA frame
    write_wrapper_->WriteUint8(FT_DATA);
    write_wrapper_->WriteVarint(3);
    write_wrapper_->WriteBytes({1, 2, 3});

    // Write HEADERS frame
    write_wrapper_->WriteUint8(FT_HEADERS);
    write_wrapper_->WriteVarint(2);
    write_wrapper_->WriteBytes({4, 5});

    // Decode first frame
    std::shared_ptr<IFrame> frame1;
    EXPECT_TRUE(DecodeFrame(read_wrapper_, frame1));
    EXPECT_EQ(frame1->GetType(), FT_DATA);

    // Decode second frame
    std::shared_ptr<IFrame> frame2;
    EXPECT_TRUE(DecodeFrame(read_wrapper_, frame2));
    EXPECT_EQ(frame2->GetType(), FT_HEADERS);
}

}  // namespace
}  // namespace http3
}  // namespace quicx 