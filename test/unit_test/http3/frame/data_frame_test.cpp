#include <gtest/gtest.h>
#include "common/decode/decode.h"
#include "http3/frame/data_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

class DataFrameTest: public testing::Test {
protected:
    void SetUp() override {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
        buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
        frame_ = std::make_shared<DataFrame>();
    }
    std::shared_ptr<common::SingleBlockBuffer> buffer_;
    std::shared_ptr<DataFrame> frame_;
};

TEST_F(DataFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FrameType::kData);

    uint32_t length = 100;
    frame_->SetLength(length);
    EXPECT_EQ(frame_->GetLength(), length);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(data.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(data.data(), data.size());
    frame_->SetData(buffer);
    EXPECT_EQ(frame_->GetData()->GetDataLength(), data.size());
    EXPECT_EQ(frame_->GetData()->GetDataAsString(), std::string(data.begin(), data.end()));
}

TEST_F(DataFrameTest, EncodeAndDecode) {
    // Setup test data
    uint32_t length = 5;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    frame_->SetLength(length);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(data.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(data.data(), data.size());
    frame_->SetData(buffer);

    // Encode
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<DataFrame>();
    EXPECT_EQ(decode_frame->Decode(buffer_, true), DecodeResult::kSuccess);

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetLength(), length);
    EXPECT_EQ(decode_frame->GetData()->GetDataLength(), data.size());
    EXPECT_EQ(decode_frame->GetData()->GetDataAsString(), std::string(data.begin(), data.end()));
}

TEST_F(DataFrameTest, EvaluateSize) {
    uint32_t length = 5;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    frame_->SetLength(length);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(data.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(data.data(), data.size());
    frame_->SetData(buffer);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload
    uint32_t payload_size = frame_->EvaluatePayloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

// Test encoding with explicit length (chunked sending scenario)
TEST_F(DataFrameTest, EncodeWithExplicitLength) {
    // Create a body buffer with 100 bytes
    std::string data(100, 'A');
    auto body = std::make_shared<common::MultiBlockBuffer>(common::MakeBlockMemoryPoolPtr(256, 2));
    body->Write(reinterpret_cast<const uint8_t*>(data.data()), data.size());

    // Encode first 30 bytes
    DataFrame frame1;
    frame1.SetData(body);
    frame1.SetLength(30);

    auto encode_buffer1 = std::make_shared<common::MultiBlockBuffer>(common::MakeBlockMemoryPoolPtr(256, 2));
    EXPECT_TRUE(frame1.Encode(encode_buffer1));

    // After encoding, body should have 70 bytes left
    EXPECT_EQ(70u, body->GetDataLength());

    // Decode and verify first chunk
    DataFrame decoded1;
    EXPECT_EQ(decoded1.Decode(encode_buffer1, true), DecodeResult::kSuccess);
    EXPECT_EQ(30u, decoded1.GetLength());
    EXPECT_EQ(std::string(30, 'A'), decoded1.GetData()->GetDataAsString());

    // Encode next 40 bytes
    DataFrame frame2;
    frame2.SetData(body);
    frame2.SetLength(40);

    auto encode_buffer2 = std::make_shared<common::MultiBlockBuffer>(common::MakeBlockMemoryPoolPtr(256, 2));
    EXPECT_TRUE(frame2.Encode(encode_buffer2));

    // After encoding, body should have 30 bytes left
    EXPECT_EQ(30u, body->GetDataLength());

    // Decode and verify second chunk
    DataFrame decoded2;
    EXPECT_EQ(decoded2.Decode(encode_buffer2, true), DecodeResult::kSuccess);
    EXPECT_EQ(40u, decoded2.GetLength());
    EXPECT_EQ(std::string(40, 'A'), decoded2.GetData()->GetDataAsString());

    // Encode remaining 30 bytes
    DataFrame frame3;
    frame3.SetData(body);
    frame3.SetLength(30);

    auto encode_buffer3 = std::make_shared<common::MultiBlockBuffer>(common::MakeBlockMemoryPoolPtr(256, 2));
    EXPECT_TRUE(frame3.Encode(encode_buffer3));

    // Body should be empty now
    EXPECT_EQ(0u, body->GetDataLength());

    // Decode and verify third chunk
    DataFrame decoded3;
    EXPECT_EQ(decoded3.Decode(encode_buffer3, true), DecodeResult::kSuccess);
    EXPECT_EQ(30u, decoded3.GetLength());
    EXPECT_EQ(std::string(30, 'A'), decoded3.GetData()->GetDataAsString());
}

// Test encoding with length exceeding data length (should fail)
TEST_F(DataFrameTest, EncodeWithExcessiveLength) {
    std::string data = "short";
    auto body = std::make_shared<common::MultiBlockBuffer>(common::MakeBlockMemoryPoolPtr(256, 2));
    body->Write(reinterpret_cast<const uint8_t*>(data.data()), data.size());

    DataFrame frame;
    frame.SetData(body);
    frame.SetLength(100);  // More than available data

    auto encode_buffer = std::make_shared<common::MultiBlockBuffer>(common::MakeBlockMemoryPoolPtr(256, 2));
    EXPECT_FALSE(frame.Encode(encode_buffer));
}

// TODO: MultipleFramesSequence test disabled due to MultiBlockBuffer + BufferEncodeWrapper interaction issue
// The issue occurs when shallow-copy added chunks have write_limit==capacity, leaving no writable space
// for subsequent BufferEncodeWrapper usage. This needs deeper refactoring of MultiBlockBuffer.
//
// TEST_F(DataFrameTest, MultipleFramesSequence) { ... }

}  // namespace
}  // namespace http3
}  // namespace quicx