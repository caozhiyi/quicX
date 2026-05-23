#include <gtest/gtest.h>

#include "http3/frame/data_frame.h"
#include "http3/frame/frame_decoder.h"
#include "http3/frame/goaway_frame.h"
#include "http3/frame/headers_frame.h"
#include "http3/frame/push_promise_frame.h"
#include "http3/frame/settings_frame.h"

#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

class FrameDecodeTest: public testing::Test {
protected:
    void SetUp() override {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
        buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
    }
    std::shared_ptr<common::SingleBlockBuffer> buffer_;
};

TEST_F(FrameDecodeTest, DecodeDataFrame) {
    DataFrame data_frame;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(data.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(data.data(), data.size());
    data_frame.SetData(buffer);
    data_frame.Encode(buffer_);

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kData);

    auto decode_frame = std::dynamic_pointer_cast<DataFrame>(frame);
    EXPECT_NE(decode_frame, nullptr);
    EXPECT_EQ(decode_frame->GetData()->GetDataLength(), data.size());
    EXPECT_EQ(decode_frame->GetData()->GetDataAsString(), std::string(data.begin(), data.end()));
}

TEST_F(FrameDecodeTest, DecodeHeadersFrame) {
    HeadersFrame headers_frame;
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(fields.data(), fields.size());
    headers_frame.SetEncodedFields(buffer);
    headers_frame.Encode(buffer_);

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
    EXPECT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kHeaders);

    auto decode_frame = std::dynamic_pointer_cast<HeadersFrame>(frame);
    EXPECT_NE(decode_frame, nullptr);
    EXPECT_EQ(decode_frame->GetEncodedFields()->GetDataAsString(), std::string(fields.begin(), fields.end()));
}

TEST_F(FrameDecodeTest, DecodeSettingsFrame) {
    SettingsFrame settings_frame;
    settings_frame.SetSetting(1, 100);
    settings_frame.SetSetting(2, 200);
    settings_frame.Encode(buffer_);

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
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
    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
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
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(fields.data(), fields.size());
    push_promise_frame.SetEncodedFields(buffer);
    push_promise_frame.Encode(buffer_);
    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
    ASSERT_EQ(frames.size(), 1);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kPushPromise);

    auto decode_frame = std::dynamic_pointer_cast<PushPromiseFrame>(frame);
    EXPECT_NE(decode_frame, nullptr);
    EXPECT_EQ(decode_frame->GetPushId(), 100);
    EXPECT_EQ(decode_frame->GetEncodedFields()->GetDataAsString(), std::string(fields.begin(), fields.end()));
}

TEST_F(FrameDecodeTest, DecodeUnknownFrameTypeSkipped) {
    // RFC 9114 Section 9: unknown frame types MUST be ignored.
    // Write an unknown frame type (0x21 = reserved/unknown) with a valid length + payload.
    common::BufferEncodeWrapper write_wrapper(buffer_);
    write_wrapper.EncodeVarint(0x21);  // Unknown frame type (0x21 is a GREASE-like value)
    write_wrapper.EncodeVarint(3);     // Payload length = 3 bytes
    write_wrapper.EncodeFixedUint8(0xAA);
    write_wrapper.EncodeFixedUint8(0xBB);
    write_wrapper.EncodeFixedUint8(0xCC);
    write_wrapper.Flush();

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    // Decoder should succeed (skip unknown frame) and produce no frames
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeCorruptVarintFrameType) {
    // Write 0xFF which starts an 8-byte varint (high 2 bits = 11) but only provide 1 byte.
    // The varint decode will fail because there are not enough bytes to complete it,
    // and the decoder correctly identifies this as corrupt data (not just "need more data")
    // since the buffer still has unread data after the failed decode attempt.
    common::BufferEncodeWrapper write_wrapper(buffer_);
    write_wrapper.EncodeFixedUint8(0xFF);
    write_wrapper.Flush();

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_FALSE(decoder.DecodeFrames(buffer_, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeUnknownFrameTypeMissingLength) {
    // Write a complete single-byte varint for an unknown frame type (e.g., 0x21),
    // but don't provide the length field. The decoder should return true (need more data).
    common::BufferEncodeWrapper write_wrapper(buffer_);
    write_wrapper.EncodeVarint(0x21);  // Single-byte varint, unknown frame type
    write_wrapper.Flush();

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    // Decoder reads frame type (0x21), finds it unknown, tries to read length varint,
    // but no data left → returns true (need more data).
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeEmptyBuffer) {
    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;
    EXPECT_FALSE(decoder.DecodeFrames(buffer_, frames));
    EXPECT_EQ(frames.size(), 0);
}

TEST_F(FrameDecodeTest, DecodeIncompleteFrame) {
    // Write only frame type without payload
    common::BufferEncodeWrapper write_wrapper(buffer_);
    write_wrapper.EncodeFixedUint8(static_cast<uint8_t>(FrameType::kData));
    write_wrapper.Flush();
    std::vector<std::shared_ptr<IFrame>> frames;
    FrameDecoder decoder;
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
    EXPECT_EQ(frames.size(), 0);
}

// ============================================================================
// Regression tests for TODO #24 — HTTP/3 FrameDecoder unknown frame type
// state corruption when length varint is incomplete across two OnData calls.
//
// Bug:
//   When an unknown frame's TYPE varint completes a buffer chunk but the
//   LENGTH varint has not yet arrived, the decoder consumes the type bytes
//   but does NOT remember that fact. On the next call it re-enters
//   kReadingFrameType and mis-decodes the LENGTH bytes as a new TYPE.
//
// Repro layout (single decoder instance, 2 buffers simulating 2 OnData calls):
//   Buffer #1: [type=0x21]                       (varint 1B, complete)
//   Buffer #2: [length=3][payload AA BB CC]      (length varint + payload)
// Expected: decoder treats the whole sequence as one unknown frame and skips
// all of it; total frames produced = 0; no error.
// Buggy behavior: on second call, 0x03 is interpreted as frame_type=CANCEL_PUSH
// and 0xAA is interpreted as its push_id varint, producing a spurious frame
// or a decode error.
// ============================================================================

// Helper: build a fresh 1024-byte buffer and write |bytes| into it.
static std::shared_ptr<common::SingleBlockBuffer> MakeBuffer(
        const std::vector<uint8_t>& bytes) {
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk);
    if (!bytes.empty()) {
        buf->Write(const_cast<uint8_t*>(bytes.data()), bytes.size());
    }
    return buf;
}

TEST_F(FrameDecodeTest, UnknownFrameType_LengthSplitAcrossTwoBuffers) {
    // OnData #1: just the type byte (0x21 — single-byte varint, unknown).
    auto buf1 = MakeBuffer({0x21});
    // OnData #2: length=3 plus 3 payload bytes.
    auto buf2 = MakeBuffer({0x03, 0xAA, 0xBB, 0xCC});

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;

    // First call must succeed (need-more-data is fine) and produce no frame.
    EXPECT_TRUE(decoder.DecodeFrames(buf1, frames));
    EXPECT_EQ(frames.size(), 0u);
    // First buffer should be fully consumed (type was read).
    EXPECT_EQ(buf1->GetDataLength(), 0u);

    // Second call: the decoder must remember it already consumed an unknown
    // frame type and treat buf2 as [length][payload], NOT as a brand-new frame.
    EXPECT_TRUE(decoder.DecodeFrames(buf2, frames));
    EXPECT_EQ(frames.size(), 0u);  // Unknown frame is skipped, no frame produced.
    EXPECT_EQ(buf2->GetDataLength(), 0u);  // All 4 bytes consumed.
}

TEST_F(FrameDecodeTest, UnknownFrameType_TwoByteLengthVarintInSecondBuffer) {
    // OnData #1: only the unknown frame type (single-byte varint = 0x21).
    auto buf1 = MakeBuffer({0x21});
    // OnData #2: 2-byte length varint encoding 100 (0x40, 0x64) + 100 payload bytes.
    // Per RFC 9000 §16: 2-byte varint has high 2 bits = 01.
    // Value = ((0x40 & 0x3F) << 8) | 0x64 = (0 << 8) | 0x64 = 100.
    std::vector<uint8_t> second;
    second.push_back(0x40);
    second.push_back(0x64);
    for (int i = 0; i < 100; i++) {
        second.push_back(0xCD);
    }
    auto buf2 = MakeBuffer(second);

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;

    EXPECT_TRUE(decoder.DecodeFrames(buf1, frames));
    EXPECT_EQ(frames.size(), 0u);
    EXPECT_EQ(buf1->GetDataLength(), 0u);

    EXPECT_TRUE(decoder.DecodeFrames(buf2, frames));
    EXPECT_EQ(frames.size(), 0u);
    EXPECT_EQ(buf2->GetDataLength(), 0u);  // All 102 bytes consumed.
}

TEST_F(FrameDecodeTest, UnknownFrameType_FollowedByValidFrameInSecondBuffer) {
    // OnData #1: unknown frame type only.
    auto buf1 = MakeBuffer({0x21});
    // OnData #2: [length=2][AA BB]   then a valid DATA frame [type=0x00][len=2][01 02].
    auto buf2 = MakeBuffer({0x02, 0xAA, 0xBB,
                            0x00, 0x02, 0x01, 0x02});

    FrameDecoder decoder;
    std::vector<std::shared_ptr<IFrame>> frames;

    EXPECT_TRUE(decoder.DecodeFrames(buf1, frames));
    EXPECT_EQ(frames.size(), 0u);

    EXPECT_TRUE(decoder.DecodeFrames(buf2, frames));
    // Unknown frame skipped; DATA frame decoded → exactly 1 frame produced.
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0]->GetType(), FrameType::kData);
    auto data = std::dynamic_pointer_cast<DataFrame>(frames[0]);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->GetData()->GetDataLength(), 2u);
    EXPECT_EQ(buf2->GetDataLength(), 0u);
}

TEST_F(FrameDecodeTest, DecodeMultipleFrames) {
    DataFrame data_frame;
    std::vector<uint8_t> data = {1, 2, 3};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(data.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(data.data(), data.size());
    data_frame.SetData(buffer);
    data_frame.Encode(buffer_);

    HeadersFrame headers_frame;
    std::vector<uint8_t> fields = {4, 5};
    auto chunk2 = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer2 = std::make_shared<common::SingleBlockBuffer>(chunk2);
    buffer->Write(fields.data(), fields.size());
    headers_frame.SetEncodedFields(buffer);
    headers_frame.Encode(buffer_);

    // Decode first frame
    std::vector<std::shared_ptr<IFrame>> frames;
    FrameDecoder decoder;
    EXPECT_TRUE(decoder.DecodeFrames(buffer_, frames));
    ASSERT_EQ(frames.size(), 2);
    auto frame = frames[0];
    EXPECT_EQ(frame->GetType(), FrameType::kData);

    frame = frames[1];
    EXPECT_EQ(frame->GetType(), FrameType::kHeaders);
}

}  // namespace
}  // namespace http3
}  // namespace quicx