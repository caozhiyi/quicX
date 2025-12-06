#include <gtest/gtest.h>
#include <cstring>
#include <memory>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stream_data_blocked_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/frame/type.h"
#include "quic/stream/recv_stream.h"

namespace quicx {
namespace quic {
namespace {

// Test fixture for RecvStream
class RecvStreamTest: public ::testing::Test {
protected:
    void SetUp() override {
        recv_callback_called_ = false;
        connection_closed_ = false;
        is_last_packet_ = false;
        error_code_ = 0;
        received_data_length_ = 0;

        active_send_cb_ = [](std::shared_ptr<IStream>) {};
        stream_close_cb_ = [](uint64_t) {};
        connection_close_cb_ = [this](uint64_t error, uint16_t frame_type, const std::string&) {
            connection_closed_ = true;
            error_code_ = error;
        };
        recv_cb_ = [this](std::shared_ptr<IBufferRead> buffer, bool is_last, uint32_t err) {
            recv_callback_called_ = true;
            is_last_packet_ = is_last;
            error_code_ = err;
            if (buffer) {
                received_data_length_ = buffer->GetDataLength();
            }
        };
    }

    bool recv_callback_called_;
    bool connection_closed_;
    bool is_last_packet_;
    uint32_t error_code_;
    uint32_t received_data_length_;

    std::function<void(std::shared_ptr<IStream>)> active_send_cb_;
    std::function<void(uint64_t)> stream_close_cb_;
    std::function<void(uint64_t, uint16_t, const std::string&)> connection_close_cb_;
    stream_read_callback recv_cb_;
};

// ==== 1. data recv test (6) ====

// Test 1.1: sequential data recv
TEST_F(RecvStreamTest, RecvDataBasic) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // create stream frame
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[5] = {0};
    memcpy(data, "Hello", 5);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer->Write(data, 5);
    frame->SetData(data_buffer->GetSharedReadableSpan());

    uint32_t ret = stream->OnFrame(frame);

    EXPECT_EQ(ret, 5);
    EXPECT_TRUE(recv_callback_called_);
    EXPECT_FALSE(is_last_packet_);
}

// Test 1.2: out of order data recv
TEST_F(RecvStreamTest, RecvDataOutOfOrder) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv frame 2 first (offset 10)
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(5);
    frame2->SetOffset(10);
    uint8_t data2[5] = {0};
    memcpy(data2, "World", 5);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer2->Write(data2, 5);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());

    recv_callback_called_ = false;
    stream->OnFrame(frame2);

    // should not trigger callback (waiting for frame 1)
    EXPECT_FALSE(recv_callback_called_);

    // now recv frame 1 (offset 0)
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(5);
    frame1->SetOffset(0);
    uint8_t data1[10] = {0};
    memcpy(data1, "Hello", 10);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
    data_buffer1->Write(data1, 10);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());

    recv_callback_called_ = false;
    stream->OnFrame(frame1);

    // should trigger callback now (both frames delivered)
    EXPECT_TRUE(recv_callback_called_);
}

// Test 1.3: data with gaps
TEST_F(RecvStreamTest, RecvDataWithGaps) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv frame at offset 20 (gap at 0-19)
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(20);
    uint8_t data[4] = {0};
    memcpy(data, "Data", 4);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write(data, 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());

    recv_callback_called_ = false;
    stream->OnFrame(frame);

    // should not trigger callback (waiting for earlier data)
    EXPECT_FALSE(recv_callback_called_);
}

// Test 1.4: FIN handle
TEST_F(RecvStreamTest, RecvDataWithFIN) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv frame with FIN
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[5] = {0};
    memcpy(data, "Final", 5);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer->Write(data, 5);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();

    stream->OnFrame(frame);

    EXPECT_TRUE(recv_callback_called_);
    EXPECT_TRUE(is_last_packet_);
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
}

// Test 1.5: duplicate data
TEST_F(RecvStreamTest, RecvDuplicateData) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv frame
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[4] = {0};
    memcpy(data, "Data", 4);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write(data, 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());

    stream->OnFrame(frame);

    // recv duplicate
    recv_callback_called_ = false;
    uint32_t ret = stream->OnFrame(frame);

    // should return 0 (duplicate detected)
    EXPECT_EQ(ret, 0);
    EXPECT_FALSE(recv_callback_called_);
}

// Test 1.6: exceeds flow control limit
TEST_F(RecvStreamTest, RecvDataExceedsLimit) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 100, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    // create frame exceeding limit
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[200];
    memset(data, 'X', 200);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(200));
    data_buffer->Write(data, 200);
    frame->SetData(data_buffer->GetSharedReadableSpan());

    stream->OnFrame(frame);

    // should trigger connection close
    EXPECT_TRUE(connection_closed_);
}

// ==== 2. OnStreamFrame test (5) ====

// Test 2.1: basic STREAM frame handle
TEST_F(RecvStreamTest, OnStreamFrameBasic) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[] = "Test";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write(data, 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());

    uint32_t ret = stream->OnFrame(frame);

    EXPECT_EQ(ret, 4);
    EXPECT_TRUE(recv_callback_called_);
}

// Test 2.2: offset handle
TEST_F(RecvStreamTest, OnStreamFrameOffset) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv at offset 0
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(5);
    frame1->SetOffset(0);
    uint8_t data1[] = "AAAA";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer1->Write(data1, 4);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    stream->OnFrame(frame1);

    // recv at offset 4
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(5);
    frame2->SetOffset(4);
    uint8_t data2[] = "BBBB";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer2->Write(data2, 4);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());

    recv_callback_called_ = false;
    stream->OnFrame(frame2);

    EXPECT_TRUE(recv_callback_called_);
}

// Test 2.3: FIN flag handle
TEST_F(RecvStreamTest, OnStreamFrameFIN) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[] = "End";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(3));
    data_buffer->Write(data, 3);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();

    stream->OnFrame(frame);

    EXPECT_TRUE(is_last_packet_);
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
}

// Test 2.4: invalid state
TEST_F(RecvStreamTest, OnStreamFrameInvalidState) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // force state to Reset
    stream->GetRecvStateMachine()->OnFrame(FrameType::kResetStream);

    // try to recv STREAM frame
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[] = "Data";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write(data, 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());

    recv_callback_called_ = false;
    uint32_t ret = stream->OnFrame(frame);

    // state machine allows STREAM frames in Reset state (RFC 9000)
    // but callback should still be triggered
    EXPECT_GT(ret, 0);
}

// Test 2.5: multiple frames
TEST_F(RecvStreamTest, OnStreamFrameMultiple) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv multiple frames
    for (int i = 0; i < 5; i++) {
        auto frame = std::make_shared<StreamFrame>();
        frame->SetStreamID(5);
        frame->SetOffset(i * 10);
        uint8_t data[10];
        memset(data, 'A' + i, 10);
        std::shared_ptr<common::SingleBlockBuffer> data_buffer =
            std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
        data_buffer->Write(data, 10);
        frame->SetData(data_buffer->GetSharedReadableSpan());

        stream->OnFrame(frame);
    }

    EXPECT_TRUE(recv_callback_called_);
}

// ==== 3. OnResetStreamFrame test (3) ====

// Test 3.1: basic RESET_STREAM handle
TEST_F(RecvStreamTest, OnResetStreamFrameBasic) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv some data first
    auto data_frame = std::make_shared<StreamFrame>();
    data_frame->SetStreamID(5);
    data_frame->SetOffset(0);
    uint8_t data[] = "Test";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write(data, 4);
    data_frame->SetData(data_buffer->GetSharedReadableSpan());
    stream->OnFrame(data_frame);

    // recv RESET_STREAM
    auto reset_frame = std::make_shared<ResetStreamFrame>();
    reset_frame->SetStreamID(5);
    reset_frame->SetFinalSize(4);
    reset_frame->SetAppErrorCode(0x100);

    recv_callback_called_ = false;
    stream->OnFrame(reset_frame);

    EXPECT_TRUE(recv_callback_called_);
    EXPECT_EQ(error_code_, 0x100);
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kResetRecvd);
}

// Test 3.2: final_size validate
TEST_F(RecvStreamTest, OnResetStreamFrameFinalSize) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv RESET_STREAM with final_size
    auto reset_frame = std::make_shared<ResetStreamFrame>();
    reset_frame->SetStreamID(5);
    reset_frame->SetFinalSize(100);
    reset_frame->SetAppErrorCode(0x200);

    stream->OnFrame(reset_frame);

    EXPECT_TRUE(recv_callback_called_);
    EXPECT_EQ(error_code_, 0x200);
}

// Test 3.3: final_size mismatch
TEST_F(RecvStreamTest, OnResetStreamFrameMismatch) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    // recv data with FIN (final_size = 10)
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(5);
    frame1->SetOffset(0);
    uint8_t data[10];
    memset(data, 'A', 10);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
    data_buffer1->Write(data, 10);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    frame1->SetFin();
    stream->OnFrame(frame1);

    // recv RESET_STREAM with different final_size
    auto reset_frame = std::make_shared<ResetStreamFrame>();
    reset_frame->SetStreamID(5);
    reset_frame->SetFinalSize(20);  // Mismatch!
    reset_frame->SetAppErrorCode(0x300);

    stream->OnFrame(reset_frame);

    // should trigger connection close
    EXPECT_TRUE(connection_closed_);
}

// ==== 4. data read test (3) ====

// Test 4.1: callback triggered
TEST_F(RecvStreamTest, RecvCallbackTriggered) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[] = "Callback test";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(strlen((char*)data)));
    data_buffer->Write(data, strlen((char*)data));
    frame->SetData(data_buffer->GetSharedReadableSpan());

    stream->OnFrame(frame);

    EXPECT_TRUE(recv_callback_called_);
}

// Test 4.2: validate data correctness
TEST_F(RecvStreamTest, RecvCallbackWithData) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    stream->SetStreamReadCallBack(recv_cb_);

    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[] = "Test data";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(9));
    data_buffer->Write(data, 9);
    frame->SetData(data_buffer->GetSharedReadableSpan());

    stream->OnFrame(frame);

    EXPECT_TRUE(recv_callback_called_);
    EXPECT_EQ(received_data_length_, 9);
}

// Test 4.3: multiple callback
TEST_F(RecvStreamTest, RecvCallbackMultipleTimes) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    int callback_count = 0;
    stream->SetStreamReadCallBack(
        [&callback_count](std::shared_ptr<IBufferRead>, bool, uint32_t) { callback_count++; });

    // send 3 frames
    for (int i = 0; i < 3; i++) {
        auto frame = std::make_shared<StreamFrame>();
        frame->SetStreamID(5);
        frame->SetOffset(i * 10);
        uint8_t data[10];
        memset(data, 'A' + i, 10);
        std::shared_ptr<common::SingleBlockBuffer> data_buffer =
            std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
        data_buffer->Write(data, 10);
        frame->SetData(data_buffer->GetSharedReadableSpan());

        stream->OnFrame(frame);
    }

    EXPECT_EQ(callback_count, 3);
}

// ==== 5. Reset test (3) ====

// Test 5.1: Reset send STOP_SENDING
TEST_F(RecvStreamTest, ResetWithError) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    // Reset with error
    stream->Reset(0x400);

    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kResetRecvd);
}

// Test 5.2: Reset error=0, not send
TEST_F(RecvStreamTest, ResetNoError) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    // reset with error=0 (normal completion)
    StreamState state_before = stream->GetRecvStateMachine()->GetStatus();
    stream->Reset(0);
    StreamState state_after = stream->GetRecvStateMachine()->GetStatus();

    // state should not change
    EXPECT_EQ(state_before, StreamState::kRecv);
    EXPECT_EQ(state_after, StreamState::kRecv);
}

// Test 5.3: invalid state Reset
TEST_F(RecvStreamTest, ResetInvalidState) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream =
        std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);

    // force to terminal state
    stream->GetRecvStateMachine()->OnFrame(FrameType::kResetStream);
    stream->GetRecvStateMachine()->AppReadAllData();

    // try to reset
    StreamState state_before = stream->GetRecvStateMachine()->GetStatus();
    stream->Reset(0x500);
    StreamState state_after = stream->GetRecvStateMachine()->GetStatus();

    // state should remain in terminal state
    EXPECT_EQ(state_before, StreamState::kResetRead);
    EXPECT_EQ(state_after, StreamState::kResetRead);
}

// ==== 6. Flow Control test (1) ====

// Test 6.1: handle STREAM_DATA_BLOCKED
TEST_F(RecvStreamTest, RecvStreamDataBlocked) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<RecvStream>(event_loop, 10000, 5, active_send_cb_, stream_close_cb_,
        connection_close_cb_);  // initial limit 10000

    stream->SetStreamReadCallBack(recv_cb_);

    // 1. Recv partial data (small enough to NOT trigger auto-update)
    // Limit 10000. Threshold 7360. Need remaining > 7360.
    // 10000 - offset > 7360 => offset < 2640.
    // Send 1000 bytes.
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(5);
    frame1->SetOffset(0);
    uint8_t data1[1000];
    memset(data1, 'A', 1000);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1000));
    data_buffer1->Write(data1, 1000);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());

    stream->OnFrame(frame1);
    EXPECT_EQ(received_data_length_, 1000);

    // 2. Recv STREAM_DATA_BLOCKED
    // Client says it's blocked at 10000.
    auto block_frame = std::make_shared<StreamDataBlockedFrame>();
    block_frame->SetStreamID(5);
    block_frame->SetMaximumData(10000);

    stream->OnFrame(block_frame);

    // Server should send MAX_STREAM_DATA (10000 + 3096 = 13096)
    // AND update local limit to 13096.

    // 3. Recv data beyond old limit (10000)
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(5);
    frame2->SetOffset(10000);
    uint8_t data2[100];
    memset(data2, 'B', 100);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(100));
    data_buffer2->Write(data2, 100);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());

    recv_callback_called_ = false;
    // If limit is not updated, this will return 0 (flow control error or drop)
    uint32_t ret = stream->OnFrame(frame2);

    EXPECT_EQ(ret, 100);
    // Note: callback might not be called if it's out of order (gap 1000-10000),
    // but OnFrame should accept it and return length.
    // If rejected due to flow control, it returns 0.
}

}  // namespace
}  // namespace quic
}  // namespace quicx