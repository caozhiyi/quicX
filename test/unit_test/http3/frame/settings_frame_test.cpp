#include <gtest/gtest.h>

#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/decode/decode.h"

#include "http3/frame/settings_frame.h"

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

TEST_F(SettingsFrameTest, RejectsTooManyEntries) {
    // DoS hardening: SettingsFrame::Decode caps the number of entries at 32.
    // Encoding 33 entries must therefore fail to decode. We hand-craft the
    // payload because SettingsFrame::Encode dedups by id (a map), which would
    // make it hard to actually emit >32 entries.
    auto big_chunk = std::make_shared<common::StandaloneBufferChunk>(4096);
    auto big_buffer = std::make_shared<common::SingleBlockBuffer>(big_chunk);

    // Each entry: varint(id) + varint(value); use small ids/values (1 byte each)
    // so 33 entries = 66 payload bytes, well within varint(1-byte length).
    static const int kEntries = 33;
    uint32_t payload_size = kEntries * 2;  // 1 byte id + 1 byte value per entry

    common::BufferEncodeWrapper enc(big_buffer);
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)FrameType::kSettings));
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)payload_size));
    for (int i = 0; i < kEntries; ++i) {
        // Use ids in [1, 33] and values equal to id; both fit in a 1-byte varint.
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)(i + 1)));
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)(i + 1)));
    }
    enc.Flush();  // commit staged writes so Decode() sees them

    auto decode_frame = std::make_shared<SettingsFrame>();
    EXPECT_EQ(decode_frame->Decode(big_buffer, true), DecodeResult::kError);
}

TEST_F(SettingsFrameTest, AcceptsMaxEntries) {
    // The 32-entry cap is inclusive: exactly 32 entries must still decode.
    auto big_chunk = std::make_shared<common::StandaloneBufferChunk>(4096);
    auto big_buffer = std::make_shared<common::SingleBlockBuffer>(big_chunk);

    static const int kEntries = 32;
    uint32_t payload_size = kEntries * 2;

    common::BufferEncodeWrapper enc(big_buffer);
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)FrameType::kSettings));
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)payload_size));
    for (int i = 0; i < kEntries; ++i) {
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)(i + 1)));
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)(i + 1)));
    }
    enc.Flush();  // commit staged writes so Decode() sees them

    auto decode_frame = std::make_shared<SettingsFrame>();
    EXPECT_EQ(decode_frame->Decode(big_buffer, true), DecodeResult::kSuccess);
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
    uint32_t expected_size = common::GetEncodeVarintLength(frame_->GetType()) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

}  // namespace
}  // namespace http3
}  // namespace quicx