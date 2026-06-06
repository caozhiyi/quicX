#include "http3/frame/goaway_frame.h"
#include <gtest/gtest.h>
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/decode/decode.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "http3/frame/type.h"

namespace quicx {
namespace http3 {
namespace {

class GoAwayFrameTest: public testing::Test {
protected:
    void SetUp() override {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
        buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
        frame_ = std::make_shared<GoAwayFrame>();
    }

    std::shared_ptr<common::SingleBlockBuffer> buffer_;
    std::shared_ptr<GoAwayFrame> frame_;
};

TEST_F(GoAwayFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FrameType::kGoAway);

    uint64_t stream_id = 100;
    frame_->SetStreamId(stream_id);
    EXPECT_EQ(frame_->GetStreamId(), stream_id);
}

TEST_F(GoAwayFrameTest, EncodeAndDecode) {
    uint64_t stream_id = 100;
    frame_->SetStreamId(stream_id);

    // Encode
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<GoAwayFrame>();
    EXPECT_EQ(decode_frame->Decode(buffer_, true), DecodeResult::kSuccess);

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetStreamId(), stream_id);
}

TEST_F(GoAwayFrameTest, EvaluateSize) {
    frame_->SetStreamId(100);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload (stream_id)
    uint32_t payload_size = frame_->EvaluatePayloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = common::GetEncodeVarintLength(frame_->GetType()) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

// RFC 9114 §7.2.6: GOAWAY payload is exactly one varint Stream/Push ID, so
// the Length field MUST equal the encoded size of that ID. The decoder must
// reject mismatches to prevent frame-stream desynchronisation by malformed
// peers (see goaway_frame.cpp `Decode`).
TEST_F(GoAwayFrameTest, RejectsLengthZero) {
    common::BufferEncodeWrapper w(buffer_);
    w.EncodeVarint(static_cast<uint64_t>(FrameType::kGoAway));
    w.EncodeVarint(static_cast<uint64_t>(0));    // bogus length = 0
    w.EncodeVarint(static_cast<uint64_t>(100));  // stream id (extra junk)
    w.Flush();

    auto f = std::make_shared<GoAwayFrame>();
    EXPECT_EQ(f->Decode(buffer_, true), DecodeResult::kError);
}

TEST_F(GoAwayFrameTest, RejectsLengthTooLarge) {
    common::BufferEncodeWrapper w(buffer_);
    w.EncodeVarint(static_cast<uint64_t>(FrameType::kGoAway));
    w.EncodeVarint(static_cast<uint64_t>(9));    // > 8: impossible varint length
    w.EncodeVarint(static_cast<uint64_t>(100));
    w.Flush();

    auto f = std::make_shared<GoAwayFrame>();
    EXPECT_EQ(f->Decode(buffer_, true), DecodeResult::kError);
}

TEST_F(GoAwayFrameTest, RejectsLengthMismatch) {
    // Encode a GOAWAY whose declared Length does not match the encoded
    // length of the stream-id varint (claim 4 bytes, write a 1-byte varint).
    common::BufferEncodeWrapper w(buffer_);
    w.EncodeVarint(static_cast<uint64_t>(FrameType::kGoAway));
    w.EncodeVarint(static_cast<uint64_t>(4));   // length claims 4 bytes
    w.EncodeVarint(static_cast<uint64_t>(7));   // stream id encodes as 1 byte
    w.Flush();

    auto f = std::make_shared<GoAwayFrame>();
    EXPECT_EQ(f->Decode(buffer_, true), DecodeResult::kError);
}

}  // namespace
}  // namespace http3
}  // namespace quicx