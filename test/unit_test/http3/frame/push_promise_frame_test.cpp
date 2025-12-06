#include <gtest/gtest.h>
#include "common/decode/decode.h"
#include "http3/frame/push_promise_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace http3 {
namespace {

class PushPromiseFrameTest: public testing::Test {
protected:
    void SetUp() override {
        auto chunk = std::make_shared<common::StandaloneBufferChunk>(1024);
        buffer_ = std::make_shared<common::SingleBlockBuffer>(chunk);
        frame_ = std::make_shared<PushPromiseFrame>();
    }

    std::shared_ptr<common::SingleBlockBuffer> buffer_;
    std::shared_ptr<PushPromiseFrame> frame_;
};

TEST_F(PushPromiseFrameTest, BasicProperties) {
    EXPECT_EQ(frame_->GetType(), FrameType::kPushPromise);

    uint64_t push_id = 100;
    frame_->SetPushId(push_id);
    EXPECT_EQ(frame_->GetPushId(), push_id);

    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(fields.data(), fields.size());
    frame_->SetEncodedFields(buffer);
    EXPECT_EQ(frame_->GetEncodedFields()->GetDataAsString(), std::string(fields.begin(), fields.end()));
}

TEST_F(PushPromiseFrameTest, EncodeAndDecode) {
    uint64_t push_id = 100;
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    frame_->SetPushId(push_id);
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(fields.data(), fields.size());
    frame_->SetEncodedFields(buffer);

    // Encode
    EXPECT_TRUE(frame_->Encode(buffer_));

    // Create new frame for decoding
    auto decode_frame = std::make_shared<PushPromiseFrame>();
    EXPECT_EQ(decode_frame->Decode(buffer_, true), DecodeResult::kSuccess);

    // Verify decoded data
    EXPECT_EQ(decode_frame->GetPushId(), push_id);
    EXPECT_EQ(decode_frame->GetEncodedFields()->GetDataAsString(), std::string(fields.begin(), fields.end()));
}

TEST_F(PushPromiseFrameTest, EvaluateSize) {
    frame_->SetPushId(100);
    std::vector<uint8_t> fields = {1, 2, 3, 4, 5};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(fields.size());
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    buffer->Write(fields.data(), fields.size());
    frame_->SetEncodedFields(buffer);

    // Size should include:
    // 1. frame type (2 bytes)
    // 2. length field (varint)
    // 3. payload (push_id + encoded fields)
    uint32_t payload_size = frame_->EvaluatePayloadSize();
    uint32_t length_field_size = common::GetEncodeVarintLength(payload_size);
    uint32_t expected_size = sizeof(uint16_t) + length_field_size + payload_size;

    EXPECT_EQ(expected_size, frame_->EvaluateEncodeSize());
}

}  // namespace
}  // namespace http3
}  // namespace quicx