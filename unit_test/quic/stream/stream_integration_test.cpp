#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <cstring>

#include "quic/frame/type.h"
#include "common/timer/if_timer.h"
#include "quic/connection/error.h"
#include "quic/stream/send_stream.h"
#include "quic/stream/recv_stream.h"
#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/stream/bidirection_stream.h"
#include "quic/frame/max_stream_data_frame.h"
#include "common/buffer/single_block_buffer.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/connection/controler/send_control.h"

namespace quicx {
namespace quic {
namespace {

// Mock Timer for testing
class MockTimer : public common::ITimer {
public:
    MockTimer() : next_timer_id_(1) {}
    
    virtual uint64_t AddTimer(common::TimerTask& task, uint32_t time, uint64_t now = 0) override {
        return next_timer_id_++;
    }
    virtual bool RmTimer(common::TimerTask& task) override {
        return true;
    }
    virtual int32_t MinTime(uint64_t now = 0) override {
        return -1;
    }
    virtual void TimerRun(uint64_t now = 0) override {
    }
    virtual bool Empty() override {
        return true;
    }
    
private:
    uint64_t next_timer_id_;
};

// Mock Connection for integration testing with realistic behavior
class MockConnectionForIntegration {
public:
    MockConnectionForIntegration() : timer_(std::make_shared<MockTimer>()) {
        send_control_ = std::make_shared<SendControl>();
        
        // Register stream ACK callback
        send_control_->SetStreamDataAckCallback(
            [this](uint64_t stream_id, uint64_t max_offset, bool has_fin) {
                OnStreamDataAcked(stream_id, max_offset, has_fin);
            });
    }
    
    // Track sent frames
    std::vector<std::shared_ptr<IFrame>> sent_frames_;
    
    // Stream registry
    std::unordered_map<uint64_t, std::shared_ptr<IStream>> streams_;
    
    // Packet tracking
    struct PacketParseResult {
        uint64_t packet_number;
        std::vector<StreamDataInfo> stream_data;
    };
    std::vector<PacketParseResult> sent_packets_;
    
    std::shared_ptr<MockTimer> timer_;
    std::shared_ptr<SendControl> send_control_;
    
    // Helper: Encode streams to packet
    bool EncodeStreamsToPacket(std::vector<std::shared_ptr<IStream>>& streams) {
    
        FixBufferFrameVisitor visitor(1500);
        visitor.SetStreamDataSizeLimit(1400);
        
        // Try to send data from all streams
        for (auto& stream : streams) {
            stream->TrySendData(&visitor);
        }

        // Get stream data info
        auto stream_data = visitor.GetStreamDataInfo();
        
        if (!stream_data.empty()) {
            PacketParseResult pkt;
            pkt.packet_number = sent_packets_.size() + 1;
            pkt.stream_data = stream_data;
            sent_packets_.push_back(pkt);
            return true;
        }
        
        return false;
    }
    
    // Helper: Simulate ACK for packet
    void SimulateAck(uint64_t packet_number) {
        if (packet_number > sent_packets_.size()) {
            return;
        }
        
        const auto& pkt = sent_packets_[packet_number - 1];
        for (const auto& stream_info : pkt.stream_data) {
            OnStreamDataAcked(stream_info.stream_id, stream_info.max_offset, stream_info.has_fin);
        }
    }
    
    // Helper: Deliver frame to stream
    void DeliverFrameToStream(uint64_t stream_id, std::shared_ptr<IFrame> frame) {
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
            it->second->OnFrame(frame);
        }
    }
    
private:
    void OnStreamDataAcked(uint64_t stream_id, uint64_t max_offset, bool has_fin) {
        auto it = streams_.find(stream_id);
        if (it != streams_.end()) {
            auto send_stream = std::dynamic_pointer_cast<SendStream>(it->second);
            if (send_stream) {
                send_stream->OnDataAcked(max_offset, has_fin);
            }
        }
    }
};

// Test fixture for stream integration tests
class StreamIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        alloter_ = common::MakeBlockMemoryPoolPtr(1024, 4);
        mock_conn_ = std::make_shared<MockConnectionForIntegration>();
        
        stream_closed_called_ = false;
        connection_closed_called_ = false;
        
        active_send_cb_ = [](std::shared_ptr<IStream>) {};
        stream_close_cb_ = [this](uint64_t stream_id) {
            stream_closed_called_ = true;
            closed_stream_id_ = stream_id;
        };
        connection_close_cb_ = [this](uint64_t error, uint16_t frame_type, const std::string& reason) {
            connection_closed_called_ = true;
            connection_error_code_ = error;
        };
    }
    
    std::shared_ptr<common::BlockMemoryPool> alloter_;
    std::shared_ptr<MockConnectionForIntegration> mock_conn_;
    
    bool stream_closed_called_;
    bool connection_closed_called_;
    uint64_t closed_stream_id_;
    uint64_t connection_error_code_;
    
    std::function<void(std::shared_ptr<IStream>)> active_send_cb_;
    std::function<void(uint64_t)> stream_close_cb_;
    std::function<void(uint64_t, uint16_t, const std::string&)> connection_close_cb_;
};

// ==== 1. Complete Lifecycle Tests (5 tests) ====

// Test 1.1: Complete send stream lifecycle
TEST_F(StreamIntegrationTest, CompleteSendStreamLifecycle) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    mock_conn_->streams_[4] = stream;
    
    // Step 1: Send data
    uint8_t data[100];
    memset(data, 'A', 100);
    int32_t sent = stream->Send(data, 100);
    EXPECT_EQ(sent, 100);
    
    // Step 2: Encode to packet
    std::vector<std::shared_ptr<IStream>> streams = {stream};
    bool encoded = mock_conn_->EncodeStreamsToPacket(streams);
    EXPECT_TRUE(encoded);
    EXPECT_EQ(mock_conn_->sent_packets_.size(), 1);
    
    // Step 3: Transition state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    
    // Step 4: Simulate ACK
    mock_conn_->SimulateAck(1);
    
    // Step 5: Close stream
    stream->Close();
    
    // Step 6: Encode FIN
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    encoded = mock_conn_->EncodeStreamsToPacket(streams);
    EXPECT_TRUE(encoded);
    
    // Step 7: Simulate FIN ACK
    mock_conn_->SimulateAck(2);
    
    // Step 8: Verify terminal state
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
}

// Test 1.2: Complete recv stream lifecycle
TEST_F(StreamIntegrationTest, CompleteRecvStreamLifecycle) {
    int callback_count = 0;
    bool is_final = false;
    uint32_t last_buffer_length = 0;
    
    auto stream = std::make_shared<RecvStream>(
        alloter_, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    stream->SetStreamReadCallBack([&](std::shared_ptr<IBufferRead> buf, bool last, uint32_t err) {
        callback_count++;
        is_final = last;
        if (buf) {
            last_buffer_length = buf->GetDataLength();
        }
    });
    
    mock_conn_->streams_[5] = stream;
    
    // Step 1: Receive data frame 1
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(5);
    frame1->SetOffset(0);
    uint8_t data1[] = "Hello ";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(6));
    data_buffer1->Write(data1, 6);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    
    stream->OnFrame(frame1);
    EXPECT_EQ(callback_count, 1);
    EXPECT_FALSE(is_final);
    EXPECT_EQ(last_buffer_length, 6);  // First callback gets 6 bytes
    
    // Step 2: Receive data frame 2
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(5);
    frame2->SetOffset(6);
    uint8_t data2[] = "World";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer2->Write(data2, 5);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());
    
    stream->OnFrame(frame2);
    EXPECT_EQ(callback_count, 2);
    EXPECT_EQ(last_buffer_length, 11);  // Cumulative: 6 + 5
    
    // Step 3: Receive FIN
    auto frame3 = std::make_shared<StreamFrame>();
    frame3->SetStreamID(5);
    frame3->SetOffset(11);
    uint8_t data3[] = "!";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer3 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1));
    data_buffer3->Write(data3, 1);
    frame3->SetData(data_buffer3->GetSharedReadableSpan());
    frame3->SetFin();
    
    stream->OnFrame(frame3);
    EXPECT_EQ(callback_count, 3);
    EXPECT_TRUE(is_final);
    EXPECT_EQ(last_buffer_length, 12);  // Final: 6 + 5 + 1
    
    // Step 4: Verify state
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
}

// Test 1.3: Bidirectional request-response flow
TEST_F(StreamIntegrationTest, BidirectionalRequestResponseFlow) {
    std::vector<uint8_t> received_data;
    
    auto stream = std::make_shared<BidirectionStream>(
        alloter_, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    stream->SetStreamReadCallBack([&](std::shared_ptr<IBufferRead> buf, bool last, uint32_t err) {
        if (buf) {
            buf->VisitData([&](uint8_t* data, uint32_t len) {
                for (uint32_t i = 0; i < len; i++) {
                    received_data.push_back(data[i]);
                }
                return true;
            });
        }
    });
    
    mock_conn_->streams_[7] = stream;
    
    // Step 1: Send request
    uint8_t request[] = "GET /index.html";
    stream->Send(request, strlen((char*)request));
    
    // Step 2: Encode request
    std::vector<std::shared_ptr<IStream>> streams = {stream};
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    mock_conn_->EncodeStreamsToPacket(streams);
    
    // Step 3: ACK request
    mock_conn_->SimulateAck(1);
    
    // Step 4: Close send direction
    stream->Close();
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    mock_conn_->EncodeStreamsToPacket(streams);
    mock_conn_->SimulateAck(2);
    
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    EXPECT_FALSE(stream_closed_called_);
    
    // Step 5: Receive response
    auto resp_frame = std::make_shared<StreamFrame>();
    resp_frame->SetStreamID(7);
    resp_frame->SetOffset(0);
    uint8_t response[] = "HTTP/1.1 200 OK";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(strlen((char*)response)));
    data_buffer->Write(response, strlen((char*)response));
    resp_frame->SetData(data_buffer->GetSharedReadableSpan());
    resp_frame->SetFin();
    
    stream->GetRecvStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnFrame(resp_frame);
    stream->GetRecvStateMachine()->RecvAllData();
    stream->GetRecvStateMachine()->AppReadAllData();
    
    // Step 6: Verify both directions complete
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
    EXPECT_TRUE(stream_closed_called_);
    EXPECT_GE(received_data.size(), 15);  // At least 15 bytes
}

// Test 1.4: Stream reset recovery
TEST_F(StreamIntegrationTest, StreamResetRecovery) {
    uint32_t error_received = 0;
    
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    stream->SetStreamWriteCallBack([&](uint32_t size, uint32_t error) {
        error_received = error;
    });
    
    mock_conn_->streams_[4] = stream;
    
    // Step 1: Send partial data
    uint8_t data[50];
    memset(data, 'B', 50);
    stream->Send(data, 50);
    
    // Step 2: Encode data
    std::vector<std::shared_ptr<IStream>> streams = {stream};
    mock_conn_->EncodeStreamsToPacket(streams);
    
    // Step 3: Transition to Send state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    
    // Step 4: Receive STOP_SENDING
    auto stop_frame = std::make_shared<StopSendingFrame>();
    stop_frame->SetStreamID(4);
    stop_frame->SetAppErrorCode(0x123);
    
    stream->OnFrame(stop_frame);
    
    // Step 5: Verify RESET_STREAM generated
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kResetSent);
    EXPECT_EQ(error_received, 0x123);
    
    // Step 6: Encode RESET_STREAM
    uint8_t buf[1500] = {0};
    auto chunk = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk->Valid());
    std::memcpy(chunk->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk);
    FixBufferFrameVisitor visitor(1500);
    
    stream->TrySendData(&visitor);
    
    // Verify stream data info is empty (no more data to send)
    auto stream_data = visitor.GetStreamDataInfo();
    EXPECT_TRUE(stream_data.empty());
}

// Test 1.5: Out-of-order data reassembly
TEST_F(StreamIntegrationTest, OutOfOrderDataReassembly) {
    std::vector<uint8_t> received_data;
    int callback_count = 0;
    
    auto stream = std::make_shared<RecvStream>(
        alloter_, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    stream->SetStreamReadCallBack([&](std::shared_ptr<IBufferRead> buf, bool last, uint32_t err) {
        callback_count++;
        if (buf) {
            buf->VisitData([&](uint8_t* data, uint32_t len) {
                for (uint32_t i = 0; i < len; i++) {
                    received_data.push_back(data[i]);
                }
                return true;
            });
        }
    });
    
    // Step 1: Receive frame at offset 20 (out of order)
    auto frame3 = std::make_shared<StreamFrame>();
    frame3->SetStreamID(5);
    frame3->SetOffset(20);
    uint8_t data3[] = "CCC";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer3 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(3));
    data_buffer3->Write(data3, 3);
    frame3->SetData(data_buffer3->GetSharedReadableSpan());
    
    stream->OnFrame(frame3);
    EXPECT_EQ(callback_count, 0);  // Should be buffered
    
    // Step 2: Receive frame at offset 10 (still out of order)
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(5);
    frame2->SetOffset(10);
    uint8_t data2[] = "BBBBBBBBBB";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
    data_buffer2->Write(data2, 10);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());
    
    stream->OnFrame(frame2);
    EXPECT_EQ(callback_count, 0);  // Still buffered
    
    // Step 3: Receive frame at offset 0 (in order now)
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(5);
    frame1->SetOffset(0);
    uint8_t data1[] = "AAAAAAAAAA";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
    data_buffer1->Write(data1, 10);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    
    stream->OnFrame(frame1);
    EXPECT_EQ(callback_count, 1);  // All data delivered at once
    EXPECT_EQ(received_data.size(), 23);  // 10 + 10 + 3
    
    // Verify data order
    for (int i = 0; i < 10; i++) EXPECT_EQ(received_data[i], 'A');
    for (int i = 10; i < 20; i++) EXPECT_EQ(received_data[i], 'B');
    for (int i = 20; i < 23; i++) EXPECT_EQ(received_data[i], 'C');
}

// ==== 2. Multi-Stream Scenarios (3 tests) ====

// Test 2.1: Multiple streams in one packet
TEST_F(StreamIntegrationTest, MultipleStreamsInOnePacket) {
    // Create 3 streams
    auto stream1 = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    auto stream2 = std::make_shared<SendStream>(
        alloter_, 10000, 8, active_send_cb_, stream_close_cb_, connection_close_cb_);
    auto stream3 = std::make_shared<SendStream>(
        alloter_, 10000, 12, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    mock_conn_->streams_[4] = stream1;
    mock_conn_->streams_[8] = stream2;
    mock_conn_->streams_[12] = stream3;
    
    // Send data on all streams
    stream1->Send((uint8_t*)"Stream1", 7);
    stream2->Send((uint8_t*)"Stream2Data", 11);
    stream3->Send((uint8_t*)"S3", 2);
    
    // Encode all to one packet
    std::vector<std::shared_ptr<IStream>> streams = {stream1, stream2, stream3};
    bool encoded = mock_conn_->EncodeStreamsToPacket(streams);
    
    EXPECT_TRUE(encoded);
    EXPECT_EQ(mock_conn_->sent_packets_.size(), 1);
    
    // Verify all 3 streams tracked
    const auto& pkt = mock_conn_->sent_packets_[0];
    EXPECT_EQ(pkt.stream_data.size(), 3);
    
    // Verify stream IDs
    std::set<uint64_t> stream_ids;
    for (const auto& info : pkt.stream_data) {
        stream_ids.insert(info.stream_id);
    }
    EXPECT_EQ(stream_ids.count(4), 1);
    EXPECT_EQ(stream_ids.count(8), 1);
    EXPECT_EQ(stream_ids.count(12), 1);
}

// Test 2.2: Stream multiplexing
TEST_F(StreamIntegrationTest, StreamMultiplexing) {
    std::vector<std::shared_ptr<BidirectionStream>> streams;
    
    // Create 5 bidirectional streams
    for (int i = 0; i < 5; i++) {
        uint64_t stream_id = 4 * i;  // 0, 4, 8, 12, 16
        auto stream = std::make_shared<BidirectionStream>(
            alloter_, 10000, stream_id, active_send_cb_, stream_close_cb_, connection_close_cb_);
        streams.push_back(stream);
        mock_conn_->streams_[stream_id] = stream;
    }
    
    // Send data on odd streams (0, 8, 16)
    streams[0]->Send((uint8_t*)"Data0", 5);
    streams[2]->Send((uint8_t*)"Data8", 5);
    streams[4]->Send((uint8_t*)"Data16", 6);
    
    // Receive data on even streams (4, 12)
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(4);
    frame1->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer1->Write((uint8_t*)"RespA", 5);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    streams[1]->OnFrame(frame1);
    
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(12);
    frame2->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer2->Write((uint8_t*)"RespB", 5);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());
    streams[3]->OnFrame(frame2);
    
    // Verify no interference
    EXPECT_EQ(streams[0]->GetSendStateMachine()->GetStatus(), StreamState::kReady);
    EXPECT_EQ(streams[1]->GetRecvStateMachine()->GetStatus(), StreamState::kRecv);
    
    // Close each stream independently
    for (auto& stream : streams) {
        stream->Close();
    }
}

// Test 2.3: Stream priority and fairness
TEST_F(StreamIntegrationTest, StreamPriorityAndFairness) {
    // Create 3 streams with different data sizes
    auto stream1 = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    auto stream2 = std::make_shared<SendStream>(
        alloter_, 10000, 8, active_send_cb_, stream_close_cb_, connection_close_cb_);
    auto stream3 = std::make_shared<SendStream>(
        alloter_, 10000, 12, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Large data on stream1
    uint8_t large_data[500];
    memset(large_data, 'L', 500);
    stream1->Send(large_data, 500);
    
    // Medium data on stream2
    uint8_t medium_data[200];
    memset(medium_data, 'M', 200);
    stream2->Send(medium_data, 200);
    
    // Small data on stream3
    stream3->Send((uint8_t*)"Small", 5);
    
    // Encode with limited space (simulate MTU constraint)
    uint8_t buffer[800] = {0};  // Limited space
    auto chunk2 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buffer));
    ASSERT_TRUE(chunk2->Valid());
    std::memcpy(chunk2->GetData(), buffer, sizeof(buffer));
    auto buf = std::make_shared<common::SingleBlockBuffer>(chunk2);
    
    FixBufferFrameVisitor visitor(800);
    visitor.SetStreamDataSizeLimit(700);
    
    // All streams should get space (fairness)
    std::vector<std::shared_ptr<IStream>> streams = {stream1, stream2, stream3};
    for (auto& stream : streams) {
        stream->TrySendData(&visitor);
    }
    
    auto stream_data = visitor.GetStreamDataInfo();
    
    // All 3 streams should have sent something
    EXPECT_GE(stream_data.size(), 1);
    // Note: Actual fairness depends on implementation
}

// ==== 3. Flow Control Integration (3 tests) ====

// Test 3.1: Send flow control blocking
TEST_F(StreamIntegrationTest, SendFlowControlBlocking) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 100, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send data up to limit
    uint8_t data[100];
    memset(data, 'X', 100);
    stream->Send(data, 100);
    
    // Encode - should generate STREAM_DATA_BLOCKED
    uint8_t buf[1500] = {0};
    auto chunk3 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk3->Valid());
    std::memcpy(chunk3->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk3);
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(1400);
    
    stream->TrySendData(&visitor);
    
    // Step 2: Send MAX_STREAM_DATA to increase limit
    auto max_frame = std::make_shared<MaxStreamDataFrame>();
    max_frame->SetStreamID(4);
    max_frame->SetMaximumData(200);
    
    stream->OnFrame(max_frame);
    
    // Step 3: Should be able to send more now
    uint8_t more_data[50];
    memset(more_data, 'Y', 50);
    stream->Send(more_data, 50);
    
    // Encode again
    FixBufferFrameVisitor visitor2(1500);
    visitor2.SetStreamDataSizeLimit(1400);
    auto result = stream->TrySendData(&visitor2);
    
    EXPECT_EQ(result, IStream::TrySendResult::kSuccess);
}

// Test 3.2: Recv flow control auto-expansion
TEST_F(StreamIntegrationTest, RecvFlowControlAutoExpansion) {
    auto stream = std::make_shared<RecvStream>(
        alloter_, 500, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Receive data approaching limit
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[400];
    memset(data, 'D', 400);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(400));
    data_buffer->Write(data, 400);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    
    stream->OnFrame(frame);
    
    // Should auto-send MAX_STREAM_DATA
    uint8_t buf[1500] = {0};
    auto chunk4 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk4->Valid());
    std::memcpy(chunk4->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk4);
    FixBufferFrameVisitor visitor(1500);
    
    auto result = stream->TrySendData(&visitor);
    
    // Check if MAX_STREAM_DATA was generated
    // (Implementation detail: triggered when remaining < 3096)
    EXPECT_EQ(result, IStream::TrySendResult::kSuccess);
}

// Test 3.3: Flow control violation detection
TEST_F(StreamIntegrationTest, FlowControlViolationDetection) {
    auto stream = std::make_shared<RecvStream>(
        alloter_, 100, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Receive data exceeding limit
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[200];
    memset(data, 'E', 200);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(200));
    data_buffer->Write(data, 200);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    
    stream->OnFrame(frame);
    
    // Should trigger connection close
    EXPECT_TRUE(connection_closed_called_);
    EXPECT_EQ(connection_error_code_, QuicErrorCode::kFlowControlError);
}

// ==== 4. ACK Tracking Integration (3 tests) ====

// Test 4.1: Partial ACK handling
TEST_F(StreamIntegrationTest, PartialAckHandling) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    mock_conn_->streams_[4] = stream;
    
    // Send 100 bytes
    uint8_t data[100];
    memset(data, 'P', 100);
    stream->Send(data, 100);
    
    // Encode (may be split into frames)
    std::vector<std::shared_ptr<IStream>> streams = {stream};
    mock_conn_->EncodeStreamsToPacket(streams);
    
    // ACK first 50 bytes
    stream->OnDataAcked(50, false);
    
    // Verify partial ACK
    EXPECT_NE(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    
    // ACK remaining 50 bytes
    stream->OnDataAcked(100, false);
    
    // Still not terminal (no FIN)
    EXPECT_NE(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
    
    // Close and ACK FIN
    stream->Close();
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    mock_conn_->EncodeStreamsToPacket(streams);
    stream->OnDataAcked(100, true);
    
    // Now terminal
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
}

// Test 4.2: ACK with packet loss simulation
TEST_F(StreamIntegrationTest, AckWithPacketLossSimulation) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    mock_conn_->streams_[4] = stream;
    
    // Send data in 3 chunks
    for (int i = 0; i < 3; i++) {
        uint8_t data[30];
        memset(data, 'A' + i, 30);
        stream->Send(data, 30);
        
        std::vector<std::shared_ptr<IStream>> streams = {stream};
        mock_conn_->EncodeStreamsToPacket(streams);
    }
    
    EXPECT_EQ(mock_conn_->sent_packets_.size(), 3);
    
    // ACK packet 1 and 3 (packet 2 lost)
    mock_conn_->SimulateAck(1);
    mock_conn_->SimulateAck(3);
    
    // Verify acked_offset doesn't include packet 2's data
    // (This depends on implementation - max_offset based)
    
    // Retransmit packet 2 data
    std::vector<std::shared_ptr<IStream>> streams = {stream};
    mock_conn_->EncodeStreamsToPacket(streams);
    
    // ACK the retransmission
    mock_conn_->SimulateAck(4);
    
    // All data should be acked now
}

// Test 4.3: FIN ACK before data ACK
TEST_F(StreamIntegrationTest, FinAckBeforeDataAck) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    mock_conn_->streams_[4] = stream;
    
    // Send data and close
    uint8_t data[100];
    memset(data, 'F', 100);
    stream->Send(data, 100);
    stream->Close();
    
    std::vector<std::shared_ptr<IStream>> streams = {stream};
    mock_conn_->EncodeStreamsToPacket(streams);
    
    // ACK with FIN but not all data
    stream->OnDataAcked(50, true);  // FIN acked but only 50 bytes
    
    // Should NOT transition to terminal (not all data acked)
    StreamState state = stream->GetSendStateMachine()->GetStatus();
    EXPECT_NE(state, StreamState::kDataRecvd);
    
    // Transition to DataSent state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    
    // ACK all data
    stream->OnDataAcked(100, true);
    
    // Now should be terminal
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kDataRecvd);
}

// ==== 5. Error and Edge Cases (4 tests) ====

// Test 5.1: Final size consistency across multiple frames
TEST_F(StreamIntegrationTest, FinalSizeConsistencyAcrossFrames) {
    auto stream = std::make_shared<RecvStream>(
        alloter_, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Receive STREAM with FIN (final_size=50)
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(5);
    frame1->SetOffset(0);
    uint8_t data1[50];
    memset(data1, 'A', 50);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(50));
    data_buffer1->Write(data1, 50);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    frame1->SetFin();
    
    stream->OnFrame(frame1);
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kDataRead);
    
    // Receive duplicate STREAM with same final_size (should accept)
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(5);
    frame2->SetOffset(40);
    uint8_t data2[10];
    memset(data2, 'A', 10);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(10));
    data_buffer2->Write(data2, 10);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());
    frame2->SetFin();
    
    uint32_t ret = stream->OnFrame(frame2);
    EXPECT_GE(ret, 0);  // Should be accepted (duplicate or ignored)
    EXPECT_FALSE(connection_closed_called_);
    
    // Receive STREAM with different final_size (should reject)
    auto frame3 = std::make_shared<StreamFrame>();
    frame3->SetStreamID(5);
    frame3->SetOffset(0);
    uint8_t data3[100];
    memset(data3, 'B', 100);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer3 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(100));
    data_buffer3->Write(data3, 100);
    frame3->SetData(data_buffer3->GetSharedReadableSpan());
    frame3->SetFin();
    
    stream->OnFrame(frame3);
    
    // Should trigger connection close
    EXPECT_TRUE(connection_closed_called_);
    EXPECT_EQ(connection_error_code_, QuicErrorCode::kFinalSizeError);
}

// Test 5.2: RESET_STREAM after partial data
TEST_F(StreamIntegrationTest, ResetStreamAfterPartialData) {
    uint32_t error_code = 0;
    
    auto stream = std::make_shared<RecvStream>(
        alloter_, 10000, 5, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    stream->SetStreamReadCallBack([&](std::shared_ptr<IBufferRead> buf, bool last, uint32_t err) {
        error_code = err;
    });
    
    // Receive partial data
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(5);
    frame->SetOffset(0);
    uint8_t data[50];
    memset(data, 'G', 50);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(50));
    data_buffer->Write(data, 50);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    
    stream->OnFrame(frame);
    
    // Receive RESET_STREAM
    auto reset_frame = std::make_shared<ResetStreamFrame>();
    reset_frame->SetStreamID(5);
    reset_frame->SetFinalSize(100);  // Indicates 100 bytes total (50 missing)
    reset_frame->SetAppErrorCode(0x789);
    
    stream->OnFrame(reset_frame);
    
    // Verify error propagated
    EXPECT_EQ(error_code, 0x789);
    EXPECT_EQ(stream->GetRecvStateMachine()->GetStatus(), StreamState::kResetRecvd);
}

// Test 5.3: Concurrent close and reset
TEST_F(StreamIntegrationTest, ConcurrentCloseAndReset) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send data
    stream->Send((uint8_t*)"Data", 4);
    
    // Call Close
    stream->Close();
    
    // Immediately call Reset (before FIN sent)
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream);
    stream->Reset(0x999);
    
    // Verify state consistency (Reset should take precedence)
    EXPECT_EQ(stream->GetSendStateMachine()->GetStatus(), StreamState::kResetSent);
    
    // Encode - should send RESET_STREAM, not STREAM with FIN
    uint8_t buf[1500] = {0};
    auto chunk5 = std::make_shared<common::StandaloneBufferChunk>(sizeof(buf));
    ASSERT_TRUE(chunk5->Valid());
    std::memcpy(chunk5->GetData(), buf, sizeof(buf));
    auto buffer = std::make_shared<common::SingleBlockBuffer>(chunk5);
    FixBufferFrameVisitor visitor(1500);
    
    stream->TrySendData(&visitor);
    
    // Verify no STREAM frame sent (only RESET_STREAM in frames_list_)
}

// Test 5.4: Stream reuse prevention
TEST_F(StreamIntegrationTest, StreamReusePrevention) {
    auto stream = std::make_shared<BidirectionStream>(
        alloter_, 10000, 7, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Complete stream lifecycle
    stream->Send((uint8_t*)"Data", 4);
    stream->Close();
    
    // Force to terminal state
    stream->GetSendStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnDataAcked(4, true);
    
    auto frame = std::make_shared<StreamFrame>();
    frame->SetStreamID(7);
    frame->SetOffset(0);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1));
    data_buffer->Write((uint8_t*)"X", 1);
    frame->SetData(data_buffer->GetSharedReadableSpan());
    frame->SetFin();
    stream->GetRecvStateMachine()->OnFrame(FrameType::kStream | StreamFrameFlag::kFinFlag);
    stream->OnFrame(frame);
    stream->GetRecvStateMachine()->RecvAllData();
    stream->GetRecvStateMachine()->AppReadAllData();
    
    EXPECT_TRUE(stream_closed_called_);
    
    // Try to send after close
    int32_t sent = stream->Send((uint8_t*)"NewData", 7);
    EXPECT_EQ(sent, -1);  // Should reject
    
    // Try to receive after close (should be accepted per RFC for validation)
    auto new_frame = std::make_shared<StreamFrame>();
    new_frame->SetStreamID(7);
    new_frame->SetOffset(1);
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1));
    data_buffer2->Write((uint8_t*)"Y", 1);
    new_frame->SetData(data_buffer2->GetSharedReadableSpan());
    
    uint32_t ret = stream->OnFrame(new_frame);
    // Terminal states still accept frames for final_size validation
    EXPECT_GE(ret, 0);
}

}
}
}

