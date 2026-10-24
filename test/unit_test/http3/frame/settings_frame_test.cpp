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
    // ids 0x02..0x05 are HTTP/2-reserved and MUST be rejected on decode
    // (RFC 9114 §7.2.4.1), so tests use legal ids 0x01, 0x06, 0x07.
    frame_->SetSetting(1, 100);
    frame_->SetSetting(6, 200);
    frame_->SetSetting(7, 300);

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
        // Use ids in [6, 38] (skipping the reserved 0x02..0x05) and values
        // equal to id; both fit in a 1-byte varint.
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)(i + 6)));
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)(i + 6)));
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
        // ids in [6, 37] to skip the reserved 0x02..0x05.
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)(i + 6)));
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)(i + 6)));
    }
    enc.Flush();  // commit staged writes so Decode() sees them

    auto decode_frame = std::make_shared<SettingsFrame>();
    EXPECT_EQ(decode_frame->Decode(big_buffer, true), DecodeResult::kSuccess);
}

TEST_F(SettingsFrameTest, RejectsReservedIds) {
    // RFC 9114 §7.2.4.1: ids 0x02..0x05 are reserved (HTTP/2 legacy) —
    // receipt MUST be treated as H3_SETTINGS_ERROR (decode failure here).
    for (uint64_t reserved : {2ULL, 3ULL, 4ULL, 5ULL}) {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(256);
        auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
        common::BufferEncodeWrapper enc(buf);
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)FrameType::kSettings));
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)2));  // 1-byte id + 1-byte value
        EXPECT_TRUE(enc.EncodeVarint(reserved));
        EXPECT_TRUE(enc.EncodeVarint((uint64_t)1));
        enc.Flush();

        auto decode_frame = std::make_shared<SettingsFrame>();
        EXPECT_EQ(decode_frame->Decode(buf, true), DecodeResult::kError) << "reserved id " << reserved;
    }
}

TEST_F(SettingsFrameTest, RejectsDuplicateIds) {
    // RFC 9114 §7.2.4: an identifier MUST NOT appear more than once in a
    // frame. (The old uint16_t storage also let truncated GREASE ids alias
    // real ids; full-width keys + duplicate detection close that hole.)
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(256);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    common::BufferEncodeWrapper enc(buf);
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)FrameType::kSettings));
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)4));  // two 2-byte entries
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)1));
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)100));
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)1));
    EXPECT_TRUE(enc.EncodeVarint((uint64_t)200));
    enc.Flush();

    auto decode_frame = std::make_shared<SettingsFrame>();
    EXPECT_EQ(decode_frame->Decode(buf, true), DecodeResult::kError);
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