#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "http3/frame/headers_frame.h"

namespace quicx {
namespace http3 {
namespace {

class HeadersFrameTest : public testing::Test {
protected:
    void SetUp() override {
        buffer_ = std::make_shared<common::Buffer>(buf_, sizeof(buf_));
        write_buffer_ = buffer_->GetWriteViewPtr();
        read_buffer_ = buffer_->GetReadViewPtr();
        frame_ = std::make_shared<HeadersFrame>();
    }

    uint8_t buf_[1024];
    std::shared_ptr<common::Buffer> buffer_;
    std::shared_ptr<common::IBufferWrite> write_buffer_;
    std::shared_ptr<common::IBufferRead> read_buffer_;
    std::shared_ptr<HeadersFrame> frame_;
};

TEST_F(HeadersFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FT_HEADERS);
    
    uint32_t length = 100;
    frame_->SetLength(length);
    EXPECT_EQ(frame_->GetLength(), length);

    std::vector<uint8_t> fields = {0x01, 0x02, 0x03, 0x04, 0x05};
    frame_->SetEncodedFields(fields);
    EXPECT_EQ(frame_->GetEncodedFields(), fields);
}

TEST_F(HeadersFrameTest, EncodeAndDecode) {
    // Setup test data
    uint32_t length = 5;
    std::vector<uint8_t> fields = {0x01, 0x02, 0x03, 0x04, 0x05};
    frame_->SetLength(length);
    frame_->SetEncodedFields(fields);

    // Encode
    EXPECT_TRUE(frame_->Encode(write_buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<HeadersFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_buffer_, true));

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetLength(), length);
    EXPECT_EQ(decode_frame->GetEncodedFields(), fields);
}

TEST_F(HeadersFrameTest, EmptyHeadersEncodeDecode) {
    // Test with empty encoded fields
    frame_->SetLength(0);
    frame_->SetEncodedFields({});

    EXPECT_TRUE(frame_->Encode(write_buffer_));

    auto decode_frame = std::make_shared<HeadersFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_buffer_, true));

    EXPECT_EQ(decode_frame->GetLength(), 0);
    EXPECT_TRUE(decode_frame->GetEncodedFields().empty());
}

TEST_F(HeadersFrameTest, LargeHeadersEncodeDecode) {
    // Test with large encoded fields
    std::vector<uint8_t> large_fields(1000, 0x42);  // 1000 bytes of data
    frame_->SetLength(large_fields.size());
    frame_->SetEncodedFields(large_fields);

    EXPECT_TRUE(frame_->Encode(write_buffer_));

    auto decode_frame = std::make_shared<HeadersFrame>();
    EXPECT_TRUE(decode_frame->Decode(read_buffer_, true));

    EXPECT_EQ(decode_frame->GetLength(), large_fields.size());
    EXPECT_EQ(decode_frame->GetEncodedFields(), large_fields);
}

TEST_F(HeadersFrameTest, EvaluateSize) {
    std::vector<uint8_t> fields = {0x01, 0x02, 0x03, 0x04, 0x05};
    frame_->SetLength(fields.size());
    frame_->SetEncodedFields(fields);

    uint32_t expected_size = frame_->EvaluateEncodeSize();
    EXPECT_EQ(expected_size, frame_->EvaluatePaloadSize() + 1); // +1 for frame type

    // Verify size calculation with empty fields
    frame_->SetLength(0);
    frame_->SetEncodedFields({});
    EXPECT_EQ(frame_->EvaluateEncodeSize(), frame_->EvaluatePaloadSize() + 1);

    // Verify size calculation with large fields
    std::vector<uint8_t> large_fields(1000, 0x42);
    frame_->SetLength(large_fields.size());
    frame_->SetEncodedFields(large_fields);
    EXPECT_EQ(frame_->EvaluateEncodeSize(), frame_->EvaluatePaloadSize() + 1);
}

}  // namespace
}  // namespace http3
}  // namespace quicx 