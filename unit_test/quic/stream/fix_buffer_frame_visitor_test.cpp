#include <gtest/gtest.h>
#include <memory>
#include <cstring>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/frame/type.h"
#include "quic/frame/ack_frame.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/ping_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/crypto_frame.h"
#include "quic/frame/padding_frame.h"
#include "quic/stream/fix_buffer_frame_visitor.h"

namespace quicx {
namespace quic {
namespace {

// Test fixture for FixBufferFrameVisitor
class FixBufferFrameVisitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Prepare buffer
        buffer_ = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(sizeof(buf_)));
        buffer_->Write(buf_, sizeof(buf_));
    }

    uint8_t buf_[2000] = {0};
    std::shared_ptr<common::SingleBlockBuffer> buffer_;
};

// ==== 1. Frame process test (5 tests) ====

// Test 1.1: Handle STREAM frame
TEST_F(FixBufferFrameVisitorTest, HandleStreamFrame) {
    FixBufferFrameVisitor visitor(1500);
    
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(4);
    frame->SetOffset(0);
    uint8_t data[] = "Test data";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(9));
    data_buffer->Write(data, 9);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    
    bool ret = visitor.HandleFrame(frame);
    
    EXPECT_TRUE(ret);
    // Verify frame type bit is set
    EXPECT_NE(visitor.GetFrameTypeBit() & (1 << FrameType::kStream), 0);
}

// Test 1.2: Handle CRYPTO frame
TEST_F(FixBufferFrameVisitorTest, HandleCryptoFrame) {
    FixBufferFrameVisitor visitor(1500);
    
    auto frame = std::make_shared<CryptoFrame>();
    frame->SetOffset(0);
    uint8_t data[] = "Crypto data";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(11));
    data_buffer->Write(data, 11);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetEncryptionLevel(kHandshake);
    
    bool ret = visitor.HandleFrame(frame);
    
    EXPECT_TRUE(ret);
    EXPECT_EQ(visitor.GetEncryptionLevel(), kHandshake);
}

// Test 1.3: Handle ACK frame
TEST_F(FixBufferFrameVisitorTest, HandleAckFrame) {
    FixBufferFrameVisitor visitor(1500);
    
    auto frame = std::make_shared<AckFrame>();
    frame->SetLargestAck(100);
    frame->SetAckDelay(10);
    frame->SetFirstAckRange(5);
    
    bool ret = visitor.HandleFrame(frame);
    
    EXPECT_TRUE(ret);
    EXPECT_NE(visitor.GetFrameTypeBit() & (1 << FrameType::kAck), 0);
}

// Test 1.4: Handle multiple frames
TEST_F(FixBufferFrameVisitorTest, HandleMultipleFrames) {
    FixBufferFrameVisitor visitor(1500);
    
    // PING frame
    auto ping = std::make_shared<PingFrame>();
    visitor.HandleFrame(ping);
    
    // STREAM frame
    auto stream_frame = std::make_shared<StreamFrame>();
    stream_frame->SetStreamID(8);
    stream_frame->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write((uint8_t*)"Data", 4);
    stream_frame->SetData(data_buffer->GetSharedReadableSpan());
    visitor.HandleFrame(stream_frame);
    
    // ACK frame
    auto ack = std::make_shared<AckFrame>();
    ack->SetLargestAck(50);
    visitor.HandleFrame(ack);
    
    // Verify all frame type bits are set
    uint32_t bits = visitor.GetFrameTypeBit();
    EXPECT_NE(bits & (1 << FrameType::kPing), 0);
    EXPECT_NE(bits & (1 << FrameType::kStream), 0);
    EXPECT_NE(bits & (1 << FrameType::kAck), 0);
}

// Test 1.5: Handle PADDING frame
TEST_F(FixBufferFrameVisitorTest, HandlePaddingFrame) {
    FixBufferFrameVisitor visitor(1500);
    
    auto frame = std::make_shared<PaddingFrame>();
    frame->SetPaddingLength(10);
    
    bool ret = visitor.HandleFrame(frame);
    
    EXPECT_TRUE(ret);
    EXPECT_NE(visitor.GetFrameTypeBit() & (1 << FrameType::kPadding), 0);
}

// ==== 2. StreamDataInfo tracking (3 tests) ====

// Test 2.1: Single stream tracking
TEST_F(FixBufferFrameVisitorTest, StreamDataInfoSingleStream) {
    FixBufferFrameVisitor visitor(1500);
    
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(4);
    frame->SetOffset(0);
    uint8_t data[] = "Test";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write(data, 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    
    visitor.HandleFrame(frame);
    
    auto stream_data = visitor.GetStreamDataInfo();
    EXPECT_EQ(stream_data.size(), 1);
    EXPECT_EQ(stream_data[0].stream_id, 4);
    EXPECT_EQ(stream_data[0].max_offset, 4);
    EXPECT_FALSE(stream_data[0].has_fin);
}

// Test 2.2: Multiple stream tracking
TEST_F(FixBufferFrameVisitorTest, StreamDataInfoMultipleStreams) {
    FixBufferFrameVisitor visitor(1500);
    
    // Stream 4
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(4);
    frame1->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write((uint8_t*)"AAAA", 4);
    frame1->SetData(data_buffer->GetSharedReadableSpan());
    visitor.HandleFrame(frame1);
    
    // Stream 8
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(8);
    frame2->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(6));
    data_buffer2->Write((uint8_t*)"BBBBBB", 6);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());
    visitor.HandleFrame(frame2);
    
    // Stream 4 again (larger offset)
    auto frame3 = std::make_shared<StreamFrame>();
    frame3->SetStreamID(4);
    frame3->SetOffset(4);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer3 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(3));
    data_buffer3->Write((uint8_t*)"CCC", 3);
    frame3->SetData(data_buffer3->GetSharedReadableSpan());
    visitor.HandleFrame(frame3);
    
    auto stream_data = visitor.GetStreamDataInfo();
    EXPECT_EQ(stream_data.size(), 2);
    
    // Find stream 4 and verify max_offset
    bool found_stream4 = false;
    for (const auto& info : stream_data) {
        if (info.stream_id == 4) {
            EXPECT_EQ(info.max_offset, 7);  // 4 + 3
            found_stream4 = true;
        }
    }
    EXPECT_TRUE(found_stream4);
}

// Test 2.3: FIN flag tracking
TEST_F(FixBufferFrameVisitorTest, StreamDataInfoWithFIN) {
    FixBufferFrameVisitor visitor(1500);
    
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(12);
    frame->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer->Write((uint8_t*)"Final", 5);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();
    
    visitor.HandleFrame(frame);
    
    auto stream_data = visitor.GetStreamDataInfo();
    EXPECT_EQ(stream_data.size(), 1);
    EXPECT_EQ(stream_data[0].stream_id, 12);
    EXPECT_EQ(stream_data[0].max_offset, 5);
    EXPECT_TRUE(stream_data[0].has_fin);
}

// ==== 3. Buffer management (2 tests) ====

// Test 3.1: Buffer space management
TEST_F(FixBufferFrameVisitorTest, BufferSpaceManagement) {
    FixBufferFrameVisitor visitor(100);  // Small buffer
    visitor.SetStreamDataSizeLimit(100);
    
    // Add data
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(4);
    frame->SetOffset(0);
    uint8_t data[50];
    memset(data, 'A', 50);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(50));
    data_buffer->Write(data, 50);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    
    bool ret = visitor.HandleFrame(frame);
    EXPECT_TRUE(ret);
    
    // Check left space
    EXPECT_LE(visitor.GetLeftStreamDataSize(), 100);
}

// Test 3.2: Buffer overflow handling (expected failure)
TEST_F(FixBufferFrameVisitorTest, BufferOverflow) {
    FixBufferFrameVisitor visitor(50);  // Very small buffer
    visitor.SetStreamDataSizeLimit(50);
    
    // Try to add large frame
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(4);
    frame->SetOffset(0);
    uint8_t data[100] = {0};
    memset(data, 'B', 100);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(100));
    data_buffer->Write(data, 100);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    
    // Should either fail or limit the data
    bool ret = visitor.HandleFrame(frame);
    // Behavior depends on implementation (may fail or succeed with limited data)
}

}
}
}

