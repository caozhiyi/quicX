#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/settings_frame.h"
#include "common/buffer/buffer_wrapper.h"

namespace quicx {
namespace http3 {
namespace {

class SettingsFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>();
        write_wrapper_ = std::make_shared<common::BufferWrite>(buffer_);
        read_wrapper_ = std::make_shared<common::BufferRead>(buffer_);
        frame_ = std::make_shared<SettingsFrame>();
    }

    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::BufferWrite> write_wrapper_;
    std::shared_ptr<common::BufferRead> read_wrapper_;
    std::shared_ptr<SettingsFrame> frame_;
};

TEST_F(SettingsFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FT_SETTINGS);
    
    // Test setting and getting values
    uint64_t id = 1;
    uint64_t value = 100;
    frame_->SetSetting(id, value);

    uint64_t retrieved_value;
    EXPECT_TRUE(frame_->GetSetting(id, retrieved_value));
    EXPECT_EQ(retrieved_value, value);

    // Test getting non-existent setting
    EXPECT_FALSE(frame_->GetSetting(999, retrieved_value));
}

TEST_F(SettingsFrameTest, EncodeAndDecode) {
    // Setup test settings
    frame_->SetSetting(1, 100);
    frame_->SetSetting(2, 200);
    frame_->SetSetting(3, 300);

    // Encode
    EXPECT_TRUE(frame_->Encode(write_wrapper_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<SettingsFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_wrapper_, true));

    // Verify decoded settings
    uint64_t value;
    EXPECT_TRUE(decode_frame->GetSetting(1, value));
    EXPECT_EQ(value, 100);
    EXPECT_TRUE(decode_frame->GetSetting(2, value));
    EXPECT_EQ(value, 200);
    EXPECT_TRUE(decode_frame->GetSetting(3, value));
    EXPECT_EQ(value, 300);
}

TEST_F(SettingsFrameTest, EvaluateSize) {
    frame_->SetSetting(1, 100);
    frame_->SetSetting(2, 200);

    uint32_t expected_size = frame_->EvaluateEncodeSize();
    EXPECT_EQ(expected_size, frame_->EvaluatePaloadSize() + 1); // +1 for frame type
}

}  // namespace
}  // namespace http3
}  // namespace quicx 