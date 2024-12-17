#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "common/decode/decode.h"
#include "http3/frame/settings_frame.h"

namespace quicx {
namespace http3 {
namespace {

class SettingsFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
        frame_ = std::make_shared<SettingsFrame>();
    }

    uint8_t buf_[1024];
    std::shared_ptr<common::Buffer> buffer_;
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
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<SettingsFrame>();
    EXPECT_TRUE(decode_frame->Decode(buffer_, true));

    // TODO : Verify decoded settings
    // Verify decoded settings
    // uint64_t value;
    // EXPECT_TRUE(decode_frame->GetSetting(1, value));
    // EXPECT_EQ(value, 100);
    // EXPECT_TRUE(decode_frame->GetSetting(2, value));
    // EXPECT_EQ(value, 200);
    // EXPECT_TRUE(decode_frame->GetSetting(3, value));
    // EXPECT_EQ(value, 300);
}

TEST_F(SettingsFrameTest, EvaluateSize) {
    frame_->SetSetting(1, 100);
    frame_->SetSetting(2, 200);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload (settings)
    uint32_t payload_size = frame_->EvaluatePaloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

}  // namespace
}  // namespace http3
}  // namespace quicx 