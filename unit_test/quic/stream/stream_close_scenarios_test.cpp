#include <gtest/gtest.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <cstring>

#include "quic/frame/type.h"
#include "quic/stream/send_stream.h"
#include "quic/stream/recv_stream.h"
#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/stream/bidirection_stream.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/quicx/global_resource.h"

using namespace quicx;
using namespace quic;

// Test fixture for stream close scenarios
class StreamCloseScenarios : public ::testing::Test {
protected:
    void SetUp() override {
        alloter_ = common::MakeBlockMemoryPoolPtr(1024, 4);
        
        reset_sent_ = false;
        stop_sending_sent_ = false;
        stream_closed_ = false;
        
        active_send_cb_ = [](std::shared_ptr<IStream>) {};
        stream_close_cb_ = [this](uint64_t stream_id) {
            stream_closed_ = true;
            closed_stream_id_ = stream_id;
        };
        connection_close_cb_ = [](uint64_t, uint16_t, const std::string&) {};
    }

    std::shared_ptr<common::BlockMemoryPool> alloter_;
    bool reset_sent_;
    bool stop_sending_sent_;
    bool stream_closed_;
    uint64_t closed_stream_id_;
    
    std::function<void(std::shared_ptr<IStream>)> active_send_cb_;
    std::function<void(uint64_t)> stream_close_cb_;
    std::function<void(uint64_t, uint16_t, const std::string&)> connection_close_cb_;
};

// Test 1: RecvStream normal completion no STOP_SENDING
TEST_F(StreamCloseScenarios, RecvStreamNormalCompletionNoStopSending) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<RecvStream>(
        alloter_, event_loop, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // simulate receiving data with FIN
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(4);
    frame->SetOffset(0);
    uint8_t data[] = "Test";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer->Write(data, 4);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();
    
    stream->OnFrame(frame);  // use public OnFrame
    
    // check if STOP_SENDING is generated
    // TrySendData will send frames in frames_list_
    class MockVisitor : public IFrameVisitor {
    public:
        bool stop_sending_found = false;
        
        bool HandleFrame(std::shared_ptr<IFrame> frame) override {
            if (frame->GetType() == FrameType::kStopSending) {
                stop_sending_found = true;
            }
            return true;
        }
        std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
        uint8_t GetEncryptionLevel() override { return 0; }
        void SetStreamDataSizeLimit(uint32_t size) override {}
        uint32_t GetLeftStreamDataSize() override { return 1000; }
        uint64_t GetStreamDataSize() override { return 0; }
    } visitor;
    
    stream->TrySendData(&visitor);
    
    // RFC requires: normal completion should not send STOP_SENDING
    EXPECT_FALSE(visitor.stop_sending_found);
}

// Test 2: RecvStream error sends STOP_SENDING
TEST_F(StreamCloseScenarios, RecvStreamErrorSendsStopSending) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<RecvStream>(
        alloter_, event_loop, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // application layer calls Reset with error
    stream->Reset(0x123);  // error != 0
    
    // check if STOP_SENDING is generated
    class MockVisitor : public IFrameVisitor {
    public:
        bool stop_sending_found = false;
        uint32_t error_code = 0;
        
        bool HandleFrame(std::shared_ptr<IFrame> frame) override {
            if (frame->GetType() == FrameType::kStopSending) {
                stop_sending_found = true;
                auto stop = std::dynamic_pointer_cast<StopSendingFrame>(frame);
                if (stop) {
                    error_code = stop->GetAppErrorCode();
                }
            }
            return true;
        }
        std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
        uint8_t GetEncryptionLevel() override { return 0; }
        void SetStreamDataSizeLimit(uint32_t size) override {}
        uint32_t GetLeftStreamDataSize() override { return 1000; }
        uint64_t GetStreamDataSize() override { return 0; }
    } visitor;
    
    stream->TrySendData(&visitor);
    
    // RFC requires: error should send STOP_SENDING
    EXPECT_TRUE(visitor.stop_sending_found);
    EXPECT_EQ(visitor.error_code, 0x123);
}

// Test 3: BidirectionStream both directions must be completed to close
TEST_F(StreamCloseScenarios, BidirectionStreamBothDirectionsRequired) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        alloter_, event_loop, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // send direction completed
    uint8_t data[] = "Request";
    stream->Send(data, 7);
    stream->Close();
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnDataAcked(7, true);
    
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    EXPECT_FALSE(stream_closed_);  // receive direction not completed, should not close
    
    // receive direction also completed
    auto recv_frame = std::make_shared<StreamFrame>();
    recv_frame->SetStreamID(4);
    recv_frame->SetOffset(0);
    uint8_t resp[] = "Response";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(8));
    data_buffer->Write(resp, 8);
    recv_frame->SetData(data_buffer->GetSharedReadableSpan());
    recv_frame->SetFin();
    
    stream->GetRecvStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnFrame(recv_frame);  // use public OnFrame
    stream->GetRecvStateMachine()->RecvAllData();
    stream->GetRecvStateMachine()->AppReadAllData();
    
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
    
    // now both directions are completed, should close
    EXPECT_TRUE(stream_closed_);
    EXPECT_EQ(closed_stream_id_, 4);
}

// Test 4: BidirectionStream::Reset error handling
TEST_F(StreamCloseScenarios, BidirectionStreamResetWithError) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        alloter_, event_loop, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // call Reset with error
    stream->Reset(0x456);
    
    // should reset both directions
    // check if RESET_STREAM and STOP_SENDING are generated
    class MockVisitor : public IFrameVisitor {
    public:
        bool reset_stream_found = false;
        bool stop_sending_found = false;
        
        bool HandleFrame(std::shared_ptr<IFrame> frame) override {
            if (frame->GetType() == FrameType::kResetStream) {
                reset_stream_found = true;
            }
            if (frame->GetType() == FrameType::kStopSending) {
                stop_sending_found = true;
            }
            return true;
        }
        std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
        uint8_t GetEncryptionLevel() override { return 0; }
        void SetStreamDataSizeLimit(uint32_t size) override {}
        uint32_t GetLeftStreamDataSize() override { return 1000; }
        uint64_t GetStreamDataSize() override { return 0; }
    } visitor;
    
    stream->TrySendData(&visitor);
    
    // RFC requires: Reset should send RESET_STREAM and STOP_SENDING
    EXPECT_TRUE(visitor.reset_stream_found);
    EXPECT_TRUE(visitor.stop_sending_found);
}

// Test 5: BidirectionStream::Reset with error=0 should warn
TEST_F(StreamCloseScenarios, BidirectionStreamResetWithZeroWarns) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<BidirectionStream>(
        alloter_, event_loop, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // call Reset with error=0 (wrong API usage)
    stream->Reset(0);
    
    // should not send any reset frames
    class MockVisitor : public IFrameVisitor {
    public:
        bool reset_stream_found = false;
        bool stop_sending_found = false;
        
        bool HandleFrame(std::shared_ptr<IFrame> frame) override {
            if (frame->GetType() == FrameType::kResetStream) {
                reset_stream_found = true;
            }
            if (frame->GetType() == FrameType::kStopSending) {
                stop_sending_found = true;
            }
            return true;
        }
        std::shared_ptr<common::IBuffer> GetBuffer() override { return nullptr; }
        uint8_t GetEncryptionLevel() override { return 0; }
        void SetStreamDataSizeLimit(uint32_t size) override {}
        uint32_t GetLeftStreamDataSize() override { return 1000; }
        uint64_t GetStreamDataSize() override { return 0; }
    } visitor;
    
    stream->TrySendData(&visitor);
    
    // wrong API usage, should not send frames
    EXPECT_FALSE(visitor.reset_stream_found);
    EXPECT_FALSE(visitor.stop_sending_found);
}

// Test 6: SendStream receives STOP_SENDING and sends RESET_STREAM
TEST_F(StreamCloseScenarios, SendStreamRespondsToStopSending) {
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto stream = std::make_shared<SendStream>(
        alloter_, event_loop, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // send some data
    uint8_t data[] = "Test";
    stream->Send(data, 4);
    
    // first actually send data, so send_data_offset_ will be updated
    uint8_t buf[1000] = {0};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(1000);
    ASSERT_TRUE(chunk->Valid());
    std::memcpy(chunk->GetData(), buf, 1000);
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    
    class MockVisitor1 : public IFrameVisitor {
    public:
        std::shared_ptr<common::IBuffer> buf_;
        MockVisitor1(std::shared_ptr<common::IBuffer> b) : buf_(b) {}
        bool HandleFrame(std::shared_ptr<IFrame> frame) override { 
            return frame->Encode(buf_); 
        }
        std::shared_ptr<common::IBuffer> GetBuffer() override { return buf_; }
        uint8_t GetEncryptionLevel() override { return 0; }
        void SetStreamDataSizeLimit(uint32_t size) override {}
        uint32_t GetLeftStreamDataSize() override { return 1000; }
        uint64_t GetStreamDataSize() override { return 0; }
    } visitor1(buffer);
    
    stream->TrySendData(&visitor1);  // actually send, update send_data_offset_
    
    // simulate receiving STOP_SENDING (use public OnFrame interface)
    auto stop_frame = std::make_shared<StopSendingFrame>();
    stop_frame->SetStreamID(4);
    stop_frame->SetAppErrorCode(0x789);
    
    stream->OnFrame(stop_frame);
    
    // check if RESET_STREAM is generated
    uint8_t buf2[1000] = {0};
    auto chunk2 = std::make_shared<common::StandaloneBufferChunk>(1000);
    ASSERT_TRUE(chunk2->Valid());
    std::memcpy(chunk2->GetData(), buf2, 1000);
    auto buffer2 = std::make_shared<common::SingleBlockBuffer>(chunk2);
    
    class MockVisitor2 : public IFrameVisitor {
    public:
        std::shared_ptr<common::IBuffer> buf_;
        bool reset_stream_found = false;
        uint32_t error_code = 0;
        uint64_t final_size = 0;
        
        MockVisitor2(std::shared_ptr<common::IBuffer> b) : buf_(b) {}
        
        bool HandleFrame(std::shared_ptr<IFrame> frame) override {
            if (frame->GetType() == FrameType::kResetStream) {
                reset_stream_found = true;
                auto reset = std::dynamic_pointer_cast<ResetStreamFrame>(frame);
                if (reset) {
                    error_code = reset->GetAppErrorCode();
                    final_size = reset->GetFinalSize();
                }
            }
            return frame->Encode(buf_);
        }
        std::shared_ptr<common::IBuffer> GetBuffer() override { return buf_; }
        uint8_t GetEncryptionLevel() override { return 0; }
        void SetStreamDataSizeLimit(uint32_t size) override {}
        uint32_t GetLeftStreamDataSize() override { return 1000; }
        uint64_t GetStreamDataSize() override { return 0; }
    } visitor2(buffer2);
    
    stream->TrySendData(&visitor2);
    
    // RFC requires: receiving STOP_SENDING should send RESET_STREAM
    EXPECT_TRUE(visitor2.reset_stream_found);
    EXPECT_EQ(visitor2.error_code, 0x789);  // use the error provided by the other side
    EXPECT_EQ(visitor2.final_size, 4);  // final_size should be the offset of the data sent
}

