#include <gtest/gtest.h>
#include "common/decode/decode.h"
#include "http3/frame/settings_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

class SettingsFrameTest: public testing::Test {
protected:
    void SetUp() override {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
        buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
        frame_ = std::make_shared<SettingsFrame>();
    }

    std::shared_ptr<common::SingleBlockBuffer> buffer_;
    std::shared_ptr<SettingsFrame> frame_;
};

TEST_F(SettingsFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FrameType::kSettings);

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
    EXPECT_EQ(decode_frame->Decode(buffer_, true), DecodeResult::kSuccess);
}

TEST_F(SettingsFrameTest, EvaluateSize) {
    frame_->SetSetting(1, 100);
    frame_->SetSetting(2, 200);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload (settings)
    uint32_t payload_size = frame_->EvaluatePayloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

}  // namespace
}  // namespace http3
}  // namespace quicx