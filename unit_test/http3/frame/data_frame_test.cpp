#include <gtest/gtest.h>
#include "common/decode/decode.h"
#include "http3/frame/data_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

class DataFrameTest : public testing::Test {
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
    EXPECT_TRUE(decode_frame->Decode(buffer_, true));

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

}  // namespace
}  // namespace http3
}  // namespace quicx 