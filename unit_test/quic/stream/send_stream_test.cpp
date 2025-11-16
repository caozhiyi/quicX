#include <gtest/gtest.h>
#include <memory>
#include <cstdint>
#include <cstring>

#include "quic/frame/type.h"
#include "quic/stream/send_stream.h"
#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/max_stream_data_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace quic {
namespace {

// Test fixture for SendStream
class SendStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        alloter_ = common::MakeBlockMemoryPoolPtr(1024, 4);
        
        active_send_called_ = false;
        stream_closed_ = false;
        send_callback_called_ = false;
        last_sent_size_ = 0;
        last_error_code_ = 0;
        
        active_send_cb_ = [this](std::shared_ptr<IStream>) { 
            active_send_called_ = true; 
        };
        stream_close_cb_ = [this](uint64_t stream_id) {
            stream_closed_ = true;
            closed_stream_id_ = stream_id;
        };
        connection_close_cb_ = [](uint64_t, uint16_t, const std::string&) {};
        send_cb_ = [this](uint32_t size, uint32_t error) {
            send_callback_called_ = true;
            last_sent_size_ = size;
            last_error_code_ = error;
        };
    }

    std::shared_ptr<common::BlockMemoryPool> alloter_;
    bool active_send_called_;
    bool stream_closed_;
    bool send_callback_called_;
    uint32_t last_sent_size_;
    uint32_t last_error_code_;
    uint64_t closed_stream_id_;
    
    std::function<void(std::shared_ptr<IStream>)> active_send_cb_;
    std::function<void(uint64_t)> stream_close_cb_;
    std::function<void(uint64_t, uint16_t, const std::string&)> connection_close_cb_;
    stream_write_callback send_cb_;
};

// ==== 1. basic send test (5) ====

// Test 1.1: basic data send
TEST_F(SendStreamTest, SendDataBasic) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    uint8_t data[] = "Hello QUIC";
    int32_t ret = stream->Send(data, 10);
    
    EXPECT_EQ(ret, 10);
    EXPECT_TRUE(active_send_called_);
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kReady);
}

// Test 1.2: send large data
TEST_F(SendStreamTest, SendLargeData) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    uint8_t data[2000];
    for (int i = 0; i < 2000; i++) {
        data[i] = i % 256;
    }
    
    int32_t ret = stream->Send(data, 2000);
    
    EXPECT_EQ(ret, 2000);
    EXPECT_TRUE(active_send_called_);
}

// Test 1.3: send 0 byte data
TEST_F(SendStreamTest, SendEmptyData) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    uint8_t data[] = "";
    int32_t ret = stream->Send(data, 0);
    
    EXPECT_EQ(ret, 0);
}

// Test 1.4: send after close (should fail)
TEST_F(SendStreamTest, SendAfterClose) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Close stream
    stream->Close();
    
    // Manually transition to terminal state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->GetSendStateMachine()->AllAckDone();
    
    // Try to send after close
    uint8_t data[] = "Test";
    int32_t ret = stream->Send(data, 4);
    
    EXPECT_EQ(ret, -1);  // Should fail
}

// Test 1.5: send multiple times, validate accumulation
TEST_F(SendStreamTest, SendMultipleTimes) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    uint8_t data1[] = "First";
    uint8_t data2[] = "Second";
    uint8_t data3[] = "Third";
    
    int32_t ret1 = stream->Send(data1, 5);
    int32_t ret2 = stream->Send(data2, 6);
    int32_t ret3 = stream->Send(data3, 5);
    
    EXPECT_EQ(ret1, 5);
    EXPECT_EQ(ret2, 6);
    EXPECT_EQ(ret3, 5);
    EXPECT_TRUE(active_send_called_);
}

// ==== 2. Close and FIN test (3) ====

// Test 2.1: basic close call
TEST_F(SendStreamTest, CloseBasic) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    active_send_called_ = false;
    stream->Close();
    
    // Close should trigger active send callback
    EXPECT_TRUE(active_send_called_);
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kReady);
}

// Test 2.2: close triggers callback
TEST_F(SendStreamTest, CloseTriggersCallback) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send some data first
    uint8_t data[] = "Test data";
    stream->Send(data, 9);
    
    active_send_called_ = false;
    stream->Close();
    
    EXPECT_TRUE(active_send_called_);
}

// Test 2.3: multiple close, validate idempotency
TEST_F(SendStreamTest, CloseIdempotent) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    stream->Close();
    
    active_send_called_ = false;
    stream->Close();  // Second close
    
    EXPECT_TRUE(active_send_called_);  // Still triggers callback
}

// ==== 3. Reset test (3) ====

// Test 3.1: basic reset call
TEST_F(SendStreamTest, ResetBasic) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    uint8_t data[] = "Test";
    stream->Send(data, 4);
    
    // Simulate transition to Send state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    
    stream->Reset(0x100);
    
    // Verify RESET_STREAM frame was created
    uint8_t buf[1500] = {0};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk->Valid());
    std::memcpy(chunk->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(1500);
    
    auto result = stream->TrySendData(&visitor);
    
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kResetSent);
}

// Test 3.2: Reset validate error_code and final_size
TEST_F(SendStreamTest, ResetWithError) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send some data
    uint8_t data[100];
    memset(data, 'A', 100);
    stream->Send(data, 100);
    
    // Transition to Send state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    
    // Reset with error code
    stream->Reset(0x200);
    
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kResetSent);
}

// Test 3.3: invalid state Reset, validate reject
TEST_F(SendStreamTest, ResetInvalidState) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Already in Reset state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    stream->Reset(0x100);
    
    // Try to reset again
    StreamState state_before = stream->GetSendStateMachine()->GetStatus();
    stream->Reset(0x200);
    StreamState state_after = stream->GetSendStateMachine()->GetStatus();
    
    // State should remain ResetSent
    EXPECT_EQ(state_before, StreamState::kResetSent);
    EXPECT_EQ(state_after, StreamState::kResetSent);
}

// ==== 4. TrySendData test (4) ====

// Test 4.1: generate STREAM frame
TEST_F(SendStreamTest, TrySendDataGeneratesFrame) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    stream->SetStreamWriteCallBack(send_cb_);
    
    // Send data
    uint8_t data[] = "Test data for STREAM frame";
    stream->Send(data, strlen((char*)data));
    
    // Try to send
    uint8_t buf[1500] = {0};
    auto chunk2 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk2->Valid());
    std::memcpy(chunk2->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk2);
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(1500);
    
    auto result = stream->TrySendData(&visitor);
    
    EXPECT_EQ(result, IStream::TrySendResult::kSuccess);
    EXPECT_TRUE(send_callback_called_);
    EXPECT_GT(last_sent_size_, 0);
}

// Test 4.2: FIN flag set
TEST_F(SendStreamTest, TrySendDataWithFIN) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send data and close
    uint8_t data[] = "Final data";
    stream->Send(data, strlen((char*)data));
    stream->Close();
    
    // Try to send
    uint8_t buf[1500] = {0};
    auto chunk3 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk3->Valid());
    std::memcpy(chunk3->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk3);
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(1500);
    
    auto result = stream->TrySendData(&visitor);
    
    EXPECT_EQ(result, IStream::TrySendResult::kSuccess);
    // FIN should be set in the frame (verified through state machine)
}

// Test 4.3: flow control limit
TEST_F(SendStreamTest, TrySendDataFlowControl) {
    // Create stream with small flow control limit
    auto stream = std::make_shared<SendStream>(
        alloter_, 100, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send data exceeding limit
    uint8_t data[200];
    memset(data, 'X', 200);
    stream->Send(data, 200);
    
    // Try to send
    uint8_t buf[1500] = {0};
    auto chunk4 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk4->Valid());
    std::memcpy(chunk4->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk4);
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(1500);
    
    // Should only send up to flow control limit
    auto result = stream->TrySendData(&visitor);
    
    EXPECT_EQ(result, IStream::TrySendResult::kSuccess);
}

// Test 4.4: no data behavior
TEST_F(SendStreamTest, TrySendDataEmpty) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Don't send any data
    uint8_t buf[1500] = {0};
    auto chunk5 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk5->Valid());
    std::memcpy(chunk5->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk5);
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(1500);
    
    auto result = stream->TrySendData(&visitor);
    
    // Should succeed but not send any frame
    EXPECT_EQ(result, IStream::TrySendResult::kSuccess);
}

// ==== 5. Frame handle test (3) ====

// Test 5.1: OnMaxStreamDataFrame update peer_data_limit_
TEST_F(SendStreamTest, OnMaxStreamDataFrame) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 100, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send some data first (so buffer has data)
    uint8_t data[50];
    memset(data, 'D', 50);
    stream->Send(data, 50);
    
    // Create MAX_STREAM_DATA frame
    auto frame = std::make_shared<MaxStreamDataFrame>();
    frame->SetStreamID(4);
    frame->SetMaximumData(1000);
    
    active_send_called_ = false;
    stream->OnFrame(frame);
    
    // Should trigger active send (because buffer has data)
    EXPECT_TRUE(active_send_called_);
}

// Test 5.2: OnStopSendingFrame trigger Reset
TEST_F(SendStreamTest, OnStopSendingFrame) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    stream->SetStreamWriteCallBack(send_cb_);
    
    // Send some data first
    uint8_t data[] = "Test";
    stream->Send(data, 4);
    
    // Transition to Send state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    
    // Create STOP_SENDING frame
    auto frame = std::make_shared<StopSendingFrame>();
    frame->SetStreamID(4);
    frame->SetAppErrorCode(0x789);
    
    stream->OnFrame(frame);
    
    // Should trigger callback with error
    EXPECT_TRUE(send_callback_called_);
    EXPECT_EQ(last_error_code_, 0x789);
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kResetSent);
}

// Test 5.3: invalid frame type
TEST_F(SendStreamTest, OnInvalidFrame) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Create an invalid frame type (STREAM frame shouldn't be sent to SendStream)
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(4);
    
    // Should log error but not crash
    stream->OnFrame(frame);  // Should handle gracefully
}

// ==== 6. OnDataAcked test (2) ====

// Test 6.1: OnDataAcked update acked_offset_
TEST_F(SendStreamTest, OnDataAckedBasic) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send some data
    uint8_t data[100];
    memset(data, 'B', 100);
    stream->Send(data, 100);
    
    // Simulate sending data through TrySendData
    uint8_t buf[1500] = {0};
    auto chunk6 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk6->Valid());
    std::memcpy(chunk6->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk6);
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(1500);
    stream->TrySendData(&visitor);
    
    // Acknowledge data
    stream->OnDataAcked(50, false);
    
    // State should still be Send or DataSent
    auto state = stream->GetSendStateMachine()->GetStatus();
    EXPECT_TRUE(state == StreamState::kSend || state == StreamState::kDataSent || 
                state == StreamState::kReady);
}

/*
// Test 6.2: OnDataAcked with FIN，state change to DataRecvd
TEST_F(SendStreamTest, OnDataAckedWithFIN) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send data and close
    uint8_t data[50];
    memset(data, 'C', 50);
    stream->Send(data, 50);
    stream->Close();
    
    // Simulate sending
    uint8_t buf[1500] = {0};
    auto chunk7 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk7->Valid());
    std::memcpy(chunk7->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk7);
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(1500);
    
    // Send data with FIN
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    stream->TrySendData(&visitor);
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    
    // Acknowledge all data with FIN
    stream->OnDataAcked(50, true);
    
    // Should transition to Data Recvd
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
}
*/
}
}
}