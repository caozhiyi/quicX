#include <gtest/gtest.h>
#include "common/decode/decode.h"
#include "http3/frame/headers_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

class HeadersFrameTest: public testing::Test {
protected:
    void SetUp() override {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
        buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
        frame_ = std::make_shared<HeadersFrame>();
    }

    std::shared_ptr<common::SingleBlockBuffer> buffer_;
    std::shared_ptr<HeadersFrame> frame_;
};

TEST_F(HeadersFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FrameType::kHeaders);

    uint32_t length = 100;
    frame_->SetLength(length);
    EXPECT_EQ(frame_->GetLength(), length);

    std::vector<uint8_t> fields = {0x01, 0x02, 0x03, 0x04, 0x05};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(fields.data(), fields.size());
    frame_->SetEncodedFields(buffer);
    EXPECT_EQ(frame_->GetEncodedFields()->GetDataAsString(), std::string(fields.begin(), fields.end()));
}

TEST_F(HeadersFrameTest, EncodeAndDecode) {
    // Setup test data
    uint32_t length = 5;
    std::vector<uint8_t> fields = {0x01, 0x02, 0x03, 0x04, 0x05};
    frame_->SetLength(length);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(fields.data(), fields.size());
    frame_->SetEncodedFields(buffer);

    // Encode
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<HeadersFrame>();
    EXPECT_EQ(decode_frame->Decode(buffer_, true), DecodeResult::kSuccess);

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetLength(), length);
    EXPECT_EQ(decode_frame->GetEncodedFields()->GetDataAsString(), std::string(fields.begin(), fields.end()));
}

TEST_F(HeadersFrameTest, EmptyHeadersEncodeDecode) {
    // Test with empty encoded fields
    frame_->SetLength(0);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(0);
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    frame_->SetEncodedFields(buffer);

    EXPECT_TRUE(frame_->Encode(buffer_));

    auto decode_frame = std::make_shared<HeadersFrame>();
    EXPECT_EQ(decode_frame->Decode(buffer_, true), DecodeResult::kSuccess);

    EXPECT_EQ(decode_frame->GetLength(), 0);
    EXPECT_EQ(decode_frame->GetEncodedFields()->GetDataAsString(), std::string());
}

TEST_F(HeadersFrameTest, LargeHeadersEncodeDecode) {
    // Test with large encoded fields
    std::vector<uint8_t> large_fields(1000, 0x42);  // 1000 bytes of data
    frame_->SetLength(large_fields.size());
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(large_fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(large_fields.data(), large_fields.size());
    frame_->SetEncodedFields(buffer);

    EXPECT_TRUE(frame_->Encode(buffer_));

    auto decode_frame = std::make_shared<HeadersFrame>();
    EXPECT_EQ(decode_frame->Decode(buffer_, true), DecodeResult::kSuccess);

    EXPECT_EQ(decode_frame->GetLength(), large_fields.size());
    EXPECT_EQ(
        decode_frame->GetEncodedFields()->GetDataAsString(), std::string(large_fields.begin(), large_fields.end()));
}

TEST_F(HeadersFrameTest, EvaluateSize) {
    std::vector<uint8_t> fields = {0x01, 0x02, 0x03, 0x04, 0x05};
    frame_->SetLength(fields.size());
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(fields.data(), fields.size());
    frame_->SetEncodedFields(buffer);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload (encoded fields)
    uint32_t payload_size = frame_->EvaluatePayloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

}  // namespace
}  // namespace http3
}  // namespace quicx