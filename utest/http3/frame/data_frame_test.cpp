#include <gtest/gtest.h>
#include "http3/frame/data_frame.h"
#include "common/buffer/buffer.h"
#include "common/buffer/buffer_encode_wrapper.h"
#include "common/buffer/buffer_decode_wrapper.h"

namespace quicx {
namespace http3 {
namespace {

class DataFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>();
        write_wrapper_ = std::make_shared<common::BufferWrite>(buffer_);
        read_wrapper_ = std::make_shared<common::BufferRead>(buffer_);
        frame_ = std::make_shared<DataFrame>();
    }

    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::BufferWrite> write_wrapper_;
    std::shared_ptr<common::BufferRead> read_wrapper_;
    std::shared_ptr<DataFrame> frame_;
};

TEST_F(DataFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FT_DATA);
    
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
    EXPECT_TRUE(frame_->Encode(write_wrapper_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<DataFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_wrapper_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetLength(), length);
    EXPECT_EQ(decode_frame->GetData(), data);
}

TEST_F(DataFrameTest, EvaluateSize) {
    uint32_t length = 5;
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    frame_->SetLength(length);
    frame_->SetData(data);

    // Size should include frame type, length field, and payload
    uint32_t expected_size = frame_->EvaluateEncodeSize();
    EXPECT_EQ(expected_size, frame_->EvaluatePaloadSize() + 1); // +1 for frame type
}

}  // namespace
}  // namespace http3
}  // namespace quicx 