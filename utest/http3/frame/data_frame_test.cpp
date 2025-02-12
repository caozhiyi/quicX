#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "common/decode/decode.h"
#include "http3/frame/data_frame.h"

namespace quicx {
namespace http3 {
namespace {

class DataFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
        frame_ = std::make_shared<DataFrame>();
    }
    uint8_t buf_[1024];
    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<DataFrame> frame_;
};

TEST_F(DataFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FrameType::kData);
    
    uint32_t length = 100;
    frame_->SetLength(length);
    EXPECT_EQ(frame_->GetLength(), length);

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    frame_->SetData(data);
    EXPECT_EQ(frame_->GetData(), data);
}

TEST_F(DataFrameTest, EncodeAndDecode) {
    // Setup test data
    uint32_t length = 5;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    frame_->SetLength(length);
    frame_->SetData(data);

    // Encode
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<DataFrame>();
    EXPECT_TRUE(decode_frame->Decode(buffer_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetLength(), length);
    EXPECT_EQ(decode_frame->GetData(), data);
}

TEST_F(DataFrameTest, EvaluateSize) {
    uint32_t length = 5;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    frame_->SetLength(length);
    frame_->SetData(data);

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