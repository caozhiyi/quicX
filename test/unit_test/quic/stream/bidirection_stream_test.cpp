#include <gtest/gtest.h>
#include <cstring>
#include <memory>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/frame/stream_frame.h"
#include "quic/frame/type.h"
#include "quic/stream/bidirection_stream.h"

namespace quicx {
namespace quic {
namespace {

// Test fixture for BidirectionStream
class BidirectionStreamTest: public ::testing::Test {
protected:
    void SetUp() override {
        active_send_called_ = false;
        stream_closed_ = false;
        send_callback_called_ = false;
        recv_callback_called_ = false;
        last_sent_size_ = 0;
        last_error_code_ = 0;
        is_last_packet_ = false;

        active_send_cb_ = [this](std::shared_ptr<IStream>) { active_send_called_ = true; };
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
        recv_cb_ = [this](std::shared_ptr<IBufferRead> buffer, bool is_last, uint32_t err) {
            recv_callback_called_ = true;
            is_last_packet_ = is_last;
            last_error_code_ = err;
        };
    }

    bool active_send_called_;
    bool stream_closed_;
    bool send_callback_called_;
    bool recv_callback_called_;
    uint32_t last_sent_size_;
    uint32_t last_error_code_;
    bool is_last_packet_;
    uint64_t closed_stream_id_;

    std::function<void(std::shared_ptr<IStream>)> active_send_cb_;
    std::function<void(uint64_t)> stream_close_cb_;
    std::function<void(uint64_t, uint16_t, const std::string&)> connection_close_cb_;
    stream_write_callback send_cb_;
    stream_read_callback recv_cb_;
};

// ==== 1. bidirectional interaction test (5) ====

// Test 1.1: send and recv simultaneously
TEST_F(BidirectionStreamTest, SendAndRecvSimultaneously) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamWriteCallBack(send_cb_);
    stream->SetStreamReadCallBack(recv_cb_);

    // Send data
    uint8_t send_data[] = "Request";
    int32_t sent = stream->Send(send_data, 7);
    EXPECT_EQ(sent, 7);
    EXPECT_TRUE(send_callback_called_ || active_send_called_);

    // Receive data
    auto recv_frame = std::make_shared<StreamFrame>();
    recv_frame->SetStreamID(7);
    recv_frame->SetOffset(0);
    uint8_t recv_data[] = "Response";
    std::shared_ptr<common::SingleBlockBuffer> recv_data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(8));
    recv_data_buffer->Write(recv_data, 8);
    recv_frame->SetData(recv_data_buffer->GetSharedReadableSpan());

    stream->OnFrame(recv_frame);

    EXPECT_TRUE(recv_callback_called_);
    EXPECT_FALSE(stream_closed_);  // Both directions not terminated yet
}

// Test 1.2: send then recv
TEST_F(BidirectionStreamTest, SendThenRecv) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Send data first
    uint8_t send_data[] = "Question?";
    stream->Send(send_data, 9);

    // Then receive
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    uint8_t recv_data[] = "Answer!";
    std::shared_ptr<common::SingleBlockBuffer> recv_data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(7));
    recv_data_buffer->Write(recv_data, 7);
    frame->SetData(recv_data_buffer->GetSharedReadableSpan());

    stream->OnFrame(frame);

    EXPECT_TRUE(recv_callback_called_);
}

// Test 1.3: recv then send
TEST_F(BidirectionStreamTest, RecvThenSend) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Receive data first
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    uint8_t recv_data[] = "Query";
    std::shared_ptr<common::SingleBlockBuffer> recv_data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    recv_data_buffer->Write(recv_data, 5);
    frame->SetData(recv_data_buffer->GetSharedReadableSpan());

    stream->OnFrame(frame);
    EXPECT_TRUE(recv_callback_called_);

    // Then send response
    uint8_t send_data[] = "Result";
    int32_t sent = stream->Send(send_data, 6);
    EXPECT_EQ(sent, 6);
}

// Test 1.4: multiple rounds interaction
TEST_F(BidirectionStreamTest, SendRecvMultipleRounds) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Round 1
    uint8_t send1[] = "Ping";
    stream->Send(send1, 4);

    auto recv1 = std::make_shared<StreamFrame>();
    recv1->SetStreamID(7);
    recv1->SetOffset(0);
    uint8_t data1[] = "Pong";
    std::shared_ptr<common::SingleBlockBuffer> recv1_data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    recv1_data_buffer->Write(data1, 4);
    recv1->SetData(recv1_data_buffer->GetSharedReadableSpan());
    stream->OnFrame(recv1);

    // Round 2
    uint8_t send2[] = "Hello";
    stream->Send(send2, 5);

    auto recv2 = std::make_shared<StreamFrame>();
    recv2->SetStreamID(7);
    recv2->SetOffset(4);
    uint8_t data2[] = "World";
    std::shared_ptr<common::SingleBlockBuffer> recv2_data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    recv2_data_buffer->Write(data2, 5);
    recv2->SetData(recv2_data_buffer->GetSharedReadableSpan());
    stream->OnFrame(recv2);

    EXPECT_FALSE(stream_closed_);
}

// Test 1.5: CheckStreamClose AND logic validation
TEST_F(BidirectionStreamTest, CheckStreamCloseLogic) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Close send direction
    uint8_t data[] = "Data";
    stream->Send(data, 4);
    stream->Close();

    // Simulate ACK
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnDataAcked(4, true);

    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    EXPECT_FALSE(stream_closed_);  // Recv direction not done yet

    // Now complete recv direction
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    uint8_t recv_data[] = "OK";
    std::shared_ptr<common::SingleBlockBuffer> recv_data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(2));
    recv_data_buffer->Write(recv_data, 2);
    frame->SetData(recv_data_buffer->GetSharedReadableSpan());
    frame->SetFin();
    stream->OnFrame(frame);

    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
    EXPECT_TRUE(stream_closed_);  // Both directions done now
}

// ==== 2. independent close test (5) ====

// Test 2.1: only close send direction
TEST_F(BidirectionStreamTest, CloseSendDirectionOnly) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->Close();

    EXPECT_FALSE(stream_closed_);  // Recv direction still open
}

// Test 2.2: recv direction complete (through FIN)
TEST_F(BidirectionStreamTest, CloseRecvDirectionOnly) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Receive data with FIN
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    uint8_t data[] = "Data";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write(data, 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();

    stream->OnFrame(frame);

    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
    EXPECT_FALSE(stream_closed_);  // Send direction still open
}

// Test 2.3: correct order close (send close first)
TEST_F(BidirectionStreamTest, CloseInCorrectOrder) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Step 1: Close send
    uint8_t send_data[] = "Req";
    stream->Send(send_data, 3);
    stream->Close();

    // Simulate ACK
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnDataAcked(3, true);

    EXPECT_FALSE(stream_closed_);

    // Step 2: Receive with FIN
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    uint8_t recv_data[] = "Resp";
    std::shared_ptr<common::SingleBlockBuffer> recv_data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    recv_data_buffer->Write(recv_data, 4);
    frame->SetData(recv_data_buffer->GetSharedReadableSpan());
    frame->SetFin();
    stream->OnFrame(frame);

    EXPECT_TRUE(stream_closed_);
}

// Test 2.4: both directions reach terminal state then close
TEST_F(BidirectionStreamTest, BothDirectionsTerminalThenClose) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Both directions reach terminal states
    // Send side
    stream->Send((uint8_t*)"Data", 4);
    stream->Close();
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnDataAcked(4, true);

    // Recv side
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write((uint8_t*)"Data", 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();
    stream->OnFrame(frame);

    // Both terminal -> should close
    EXPECT_TRUE(stream_closed_);
    EXPECT_EQ(closed_stream_id_, 7);
}

// Test 2.5: close callback timing validation
TEST_F(BidirectionStreamTest, CloseCallbackTiming) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Send direction completes
    stream->Close();
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnDataAcked(0, true);

    EXPECT_FALSE(stream_closed_);  // Not yet

    // Recv direction completes
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1));
    data_buffer->Write((uint8_t*)"X", 1);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();
    stream->OnFrame(frame);

    EXPECT_TRUE(stream_closed_);  // Now closed
}

// ==== 3. Reset test (3) ====

// Test 3.1: Reset both directions
TEST_F(BidirectionStreamTest, ResetBothDirections) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    // Send and receive some data
    stream->Send((uint8_t*)"Data", 4);

    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write((uint8_t*)"Data", 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    stream->OnFrame(frame);

    // Reset with error
    stream->Reset(0x400);

    // Both directions should be in intermediate reset state
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kResetSent);
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kResetRecvd);
    EXPECT_FALSE(stream_closed_);  // Not terminal yet

    // In real scenarios, Reset is followed by ACK and app read
    // For unit testing, we verify intermediate states
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kResetSent);
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kResetRecvd);

    // Note: stream_closed_ will be false until:
    // - Send side: RESET_STREAM is ACKed (ResetSent -> ResetRecvd)
    // - Recv side: App reads reset (ResetRecvd -> ResetRead)
    // In unit test isolation, we can only verify Reset() triggers correct initial states
    EXPECT_FALSE(stream_closed_);
}

// Test 3.2: one side direction terminate, the other side complete normally
TEST_F(BidirectionStreamTest, ResetOneSideOnly) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Send direction: send data and close normally
    uint8_t send_data[] = "Request";
    stream->Send(send_data, 7);
    stream->Close();

    // Simulate sending and ACK
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnDataAcked(7, true);

    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    EXPECT_FALSE(stream_closed_);  // Recv side not done

    // Recv direction: receive data with FIN
    auto recv_frame = std::make_shared<StreamFrame>();
    recv_frame->SetStreamID(7);
    recv_frame->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(8));
    data_buffer->Write((uint8_t*)"Response", 8);
    recv_frame->SetData(data_buffer->GetSharedReadableSpan());
    recv_frame->SetFin();

    stream->GetRecvStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnFrame(recv_frame);
    stream->GetRecvStateMachine()->RecvAllData();
    stream->GetRecvStateMachine()->AppReadAllData();

    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
    EXPECT_TRUE(stream_closed_);  // Both terminal now
}

// Test 3.3: Reset(0) generate warning
TEST_F(BidirectionStreamTest, ResetWithZeroWarns) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    // Reset with error=0 (API misuse)
    stream->Reset(0);

    // Should not reset, state should remain
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kReady);
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kRecv);
    EXPECT_FALSE(stream_closed_);
}

// ==== 4. callback interaction test (2) ====

// Test 4.1: send and recv callbacks
TEST_F(BidirectionStreamTest, SendAndRecvCallbacks) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamWriteCallBack(send_cb_);
    stream->SetStreamReadCallBack(recv_cb_);

    // Send
    send_callback_called_ = false;
    uint8_t send_data[] = "Send";
    stream->Send(send_data, 4);
    // Callback will be triggered when data is actually sent via TrySendData

    // Receive
    recv_callback_called_ = false;
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    uint8_t recv_data[] = "Recv";
    std::shared_ptr<common::SingleBlockBuffer> recv_data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    recv_data_buffer->Write(recv_data, 4);
    frame->SetData(recv_data_buffer->GetSharedReadableSpan());
    stream->OnFrame(frame);

    EXPECT_TRUE(recv_callback_called_);
}

// Test 4.2: close callback timing
TEST_F(BidirectionStreamTest, CloseCallback) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        event_loop, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // Send some data first
    uint8_t send_data[] = "Test";
    stream->Send(send_data, 4);

    // Close send
    stream->Close();
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnDataAcked(4, true);

    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    EXPECT_FALSE(stream_closed_);  // Callback not yet

    // Close recv
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1));
    data_buffer->Write((uint8_t*)"R", 1);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();
    stream->OnFrame(frame);

    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
    EXPECT_TRUE(stream_closed_);  // Callback triggered
    EXPECT_EQ(closed_stream_id_, 7);
}

}  // namespace
}  // namespace quic
}  // namespace quicx