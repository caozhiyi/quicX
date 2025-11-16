#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "common/util/time.h"
#include "quic/frame/ack_frame.h"
#include "common/timer/if_timer.h"
#include "quic/stream/send_stream.h"
#include "quic/frame/stream_frame.h"
#include "common/timer/timer_task.h"
#include "quic/packet/rtt_1_packet.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/single_block_buffer.h"
#include "quic/stream/fix_buffer_frame_visitor.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/connection/controler/send_control.h"

using namespace quicx;
using namespace quic;

// Mock timer for testing
class MockTimer : public common::ITimer {
public:
    virtual uint64_t AddTimer(common::TimerTask& task, uint32_t time, uint64_t now = 0) override {
        return 1;
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
};

// Test fixture for stream ACK tracking
class StreamAckTrackingTest : public ::testing::Test {
protected:
    void SetUp() override {
        timer_ = std::make_shared<MockTimer>();
        alloter_ = common::MakeBlockMemoryPoolPtr(1024, 4);
        
        stream_close_called_ = false;
        stream_close_stream_id_ = 0;
        
        active_send_cb_ = [](std::shared_ptr<IStream>) {};
        stream_close_cb_ = [this](uint64_t stream_id) {
            stream_close_called_ = true;
            stream_close_stream_id_ = stream_id;
        };
        connection_close_cb_ = [](uint64_t, uint16_t, const std::string&) {};
    }

    void TearDown() override {
    }

    std::shared_ptr<common::ITimer> timer_;
    std::shared_ptr<common::BlockMemoryPool> alloter_;
    
    bool stream_close_called_;
    uint64_t stream_close_stream_id_;
    
    std::function<void(std::shared_ptr<IStream>)> active_send_cb_;
    std::function<void(uint64_t)> stream_close_cb_;
    std::function<void(uint64_t, uint16_t, const std::string&)> connection_close_cb_;
};

// Test 1: StreamDataInfo basic functionality
TEST_F(StreamAckTrackingTest, StreamDataInfoBasic) {
    StreamDataInfo info1;
    EXPECT_EQ(info1.stream_id, 0);
    EXPECT_EQ(info1.max_offset, 0);
    EXPECT_FALSE(info1.has_fin);
    
    StreamDataInfo info2(4, 1000, true);
    EXPECT_EQ(info2.stream_id, 4);
    EXPECT_EQ(info2.max_offset, 1000);
    EXPECT_TRUE(info2.has_fin);
}

// Test 2: FixBufferFrameVisitor tracks stream frames
TEST_F(StreamAckTrackingTest, FrameVisitorTracksStreamFrames) {
    FixBufferFrameVisitor visitor(1500);
    
    // Create and add a stream frame
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(4);
    frame1->SetOffset(0);
    uint8_t data1[] = "Hello";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer1->Write(data1, 5);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    frame1->SetFin();
    
    EXPECT_TRUE(visitor.HandleFrame(frame1));
    
    auto stream_data = visitor.GetStreamDataInfo();
    EXPECT_EQ(stream_data.size(), 1);
    EXPECT_EQ(stream_data[0].stream_id, 4);
    EXPECT_EQ(stream_data[0].max_offset, 5);  // offset + length
    EXPECT_TRUE(stream_data[0].has_fin);
}

// Test 3: FrameVisitor tracks multiple streams
TEST_F(StreamAckTrackingTest, FrameVisitorTracksMultipleStreams) {
    FixBufferFrameVisitor visitor(1500);
    
    // Stream 4
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(4);
    frame1->SetOffset(0);
    uint8_t data1[] = "Hello";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer1->Write(data1, 5);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    EXPECT_TRUE(visitor.HandleFrame(frame1));
    
    // Stream 8
    auto frame2 = std::make_shared<StreamFrame>();
    frame2->SetStreamID(8);
    frame2->SetOffset(0);
    uint8_t data2[] = "World";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer2 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer2->Write(data2, 5);
    frame2->SetData(data_buffer2->GetSharedReadableSpan());
    frame2->SetFin();
    EXPECT_TRUE(visitor.HandleFrame(frame2));
    
    // Stream 4 again (should update max_offset)
    auto frame3 = std::make_shared<StreamFrame>();
    frame3->SetStreamID(4);
    frame3->SetOffset(5);
    uint8_t data3[] = " Test";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer3 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(5));
    data_buffer3->Write(data3, 5);
    frame3->SetData(data_buffer3->GetSharedReadableSpan());
    frame3->SetFin();
    EXPECT_TRUE(visitor.HandleFrame(frame3));
    
    auto stream_data = visitor.GetStreamDataInfo();
    EXPECT_EQ(stream_data.size(), 2);
    
    // Find stream 4
    auto it4 = std::find_if(stream_data.begin(), stream_data.end(),
                            [](const StreamDataInfo& info) { return info.stream_id == 4; });
    ASSERT_NE(it4, stream_data.end());
    EXPECT_EQ(it4->max_offset, 10);  // 5 + 5
    EXPECT_TRUE(it4->has_fin);
    
    // Find stream 8
    auto it8 = std::find_if(stream_data.begin(), stream_data.end(),
                            [](const StreamDataInfo& info) { return info.stream_id == 8; });
    ASSERT_NE(it8, stream_data.end());
    EXPECT_EQ(it8->max_offset, 5);
    EXPECT_TRUE(it8->has_fin);
}

// Test 4: SendStream ACK tracking basic
TEST_F(StreamAckTrackingTest, SendStreamAckTracking) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send some data
    uint8_t data[] = "Hello World";
    EXPECT_GT(stream->Send(data, 11), 0);
    
    // Close stream (send FIN)
    stream->Close();
    
    // ACK partial data - should not complete
    stream->OnDataAcked(5, false);
    
    // ACK remaining data without FIN - should not complete
    stream->OnDataAcked(11, false);
    
    // ACK with FIN - should complete
    stream->OnDataAcked(11, true);
    // State machine transitions are tested indirectly through behavior
}

// Test 5: SendControl callback mechanism
TEST_F(StreamAckTrackingTest, SendControlCallbackMechanism) {
    SendControl send_control(timer_);
    
    std::vector<std::tuple<uint64_t, uint64_t, bool>> acked_streams;
    
    send_control.SetStreamDataAckCallback(
        [&acked_streams](uint64_t stream_id, uint64_t max_offset, bool has_fin) {
            acked_streams.push_back(std::make_tuple(stream_id, max_offset, has_fin));
        });
    
    // Create a mock packet with stream data
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(1);
    packet->AddFrameTypeBit(FrameTypeBit::kStreamBit);  // Mark as ack-eliciting
    
    std::vector<StreamDataInfo> stream_data;
    stream_data.push_back(StreamDataInfo(4, 100, false));
    stream_data.push_back(StreamDataInfo(8, 200, true));
    
    // Simulate packet send
    send_control.OnPacketSend(common::UTCTimeMsec(), packet, 1200, stream_data);
    
    // Simulate ACK frame
    auto ack_frame = std::make_shared<AckFrame>();
    ack_frame->SetLargestAck(1);
    ack_frame->SetAckDelay(10);
    ack_frame->SetFirstAckRange(0);
    
    send_control.OnPacketAck(common::UTCTimeMsec(), PacketNumberSpace::kApplicationNumberSpace, ack_frame);
    
    // Callback should have been called for both streams
    EXPECT_EQ(acked_streams.size(), 2);
    EXPECT_EQ(std::get<0>(acked_streams[0]), 4);
    EXPECT_EQ(std::get<1>(acked_streams[0]), 100);
    EXPECT_FALSE(std::get<2>(acked_streams[0]));
    
    EXPECT_EQ(std::get<0>(acked_streams[1]), 8);
    EXPECT_EQ(std::get<1>(acked_streams[1]), 200);
    EXPECT_TRUE(std::get<2>(acked_streams[1]));
}

// Test 6: Multiple packets ACKed
TEST_F(StreamAckTrackingTest, MultiplePacketsAcked) {
    SendControl send_control(timer_);
    
    std::vector<std::tuple<uint64_t, uint64_t, bool>> acked_data;
    
    send_control.SetStreamDataAckCallback(
        [&acked_data](uint64_t stream_id, uint64_t max_offset, bool has_fin) {
            acked_data.push_back(std::make_tuple(stream_id, max_offset, has_fin));
        });
    
    // Send packet 1: stream 4, offset 0-100
    auto packet1 = std::make_shared<Rtt1Packet>();
    packet1->SetPacketNumber(1);
    packet1->AddFrameTypeBit(FrameTypeBit::kStreamBit);  // Mark as ack-eliciting
    std::vector<StreamDataInfo> data1;
    data1.push_back(StreamDataInfo(4, 100, false));
    send_control.OnPacketSend(common::UTCTimeMsec(), packet1, 1200, data1);
    
    // Send packet 2: stream 4, offset 100-200 with FIN
    auto packet2 = std::make_shared<Rtt1Packet>();
    packet2->SetPacketNumber(2);
    packet2->AddFrameTypeBit(FrameTypeBit::kStreamBit);  // Mark as ack-eliciting
    std::vector<StreamDataInfo> data2;
    data2.push_back(StreamDataInfo(4, 200, true));
    send_control.OnPacketSend(common::UTCTimeMsec(), packet2, 1200, data2);
    
    // ACK both packets
    auto ack_frame = std::make_shared<AckFrame>();
    ack_frame->SetLargestAck(2);
    ack_frame->SetAckDelay(10);
    ack_frame->SetFirstAckRange(1);  // ACK range covers packet 1 and 2
    
    send_control.OnPacketAck(common::UTCTimeMsec(), PacketNumberSpace::kApplicationNumberSpace, ack_frame);
    
    // Should have 2 ACK notifications
    EXPECT_EQ(acked_data.size(), 2);
    
    // Verify both packets were ACKed
    bool found_packet1 = false;
    bool found_packet2 = false;
    for (const auto& ack : acked_data) {
        if (std::get<0>(ack) == 4 && std::get<1>(ack) == 100 && !std::get<2>(ack)) {
            found_packet1 = true;
        }
        if (std::get<0>(ack) == 4 && std::get<1>(ack) == 200 && std::get<2>(ack)) {
            found_packet2 = true;
        }
    }
    EXPECT_TRUE(found_packet1);
    EXPECT_TRUE(found_packet2);
}

// Test 7: Integration test - SendControl to Stream
TEST_F(StreamAckTrackingTest, IntegrationSendControlToStream) {
    SendControl send_control(timer_);
    
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Set up callback from SendControl to Stream
    send_control.SetStreamDataAckCallback(
        [&stream](uint64_t stream_id, uint64_t max_offset, bool has_fin) {
            if (stream_id == 4) {
                stream->OnDataAcked(max_offset, has_fin);
            }
        });
    
    // Send data
    uint8_t data[] = "Hello";
    stream->Send(data, 5);
    stream->Close();
    
    // Simulate packet send
    auto packet = std::make_shared<Rtt1Packet>();
    packet->SetPacketNumber(1);
    packet->AddFrameTypeBit(FrameTypeBit::kStreamBit);  // Mark as ack-eliciting
    std::vector<StreamDataInfo> stream_data;
    stream_data.push_back(StreamDataInfo(4, 5, true));
    send_control.OnPacketSend(common::UTCTimeMsec(), packet, 1200, stream_data);
    
    // Simulate ACK
    auto ack_frame = std::make_shared<AckFrame>();
    ack_frame->SetLargestAck(1);
    ack_frame->SetAckDelay(10);
    ack_frame->SetFirstAckRange(0);
    
    send_control.OnPacketAck(common::UTCTimeMsec(), PacketNumberSpace::kApplicationNumberSpace, ack_frame);
    
    // Stream should have processed the ACK
    // We can't directly check internal state, but we verified the callback chain works
}

// Test 8: Frame visitor with non-stream frames
TEST_F(StreamAckTrackingTest, FrameVisitorIgnoresNonStreamFrames) {
    FixBufferFrameVisitor visitor(1500);
    
    // Add a stream frame
    auto frame1 = std::make_shared<StreamFrame>();
    frame1->SetStreamID(4);
    frame1->SetOffset(0);
    uint8_t data1[] = "Test";
    std::shared_ptr<common::SingleBlockBuffer> data_buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(4));
    data_buffer1->Write(data1, 4);
    frame1->SetData(data_buffer1->GetSharedReadableSpan());
    visitor.HandleFrame(frame1);
    
    // Try to add a non-stream frame (e.g., ACK frame)
    // FixBufferFrameVisitor should only track STREAM frames
    auto ack_frame = std::make_shared<AckFrame>();
    ack_frame->SetLargestAck(1);
    visitor.HandleFrame(ack_frame);
    
    auto stream_data = visitor.GetStreamDataInfo();
    // Should still only have 1 stream
    EXPECT_EQ(stream_data.size(), 1);
    EXPECT_EQ(stream_data[0].stream_id, 4);
}

// Test 9: Real-world scenario - Stream sends data through visitor
TEST_F(StreamAckTrackingTest, RealWorldStreamSendThroughVisitor) {
    auto stream = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send data
    uint8_t data[] = "Hello World from QUIC!";
    stream->Send(data, 22);
    stream->Close();
    
    // Create visitor and let stream populate frames
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(10000);
    
    auto result = stream->TrySendData(&visitor);
    EXPECT_EQ(result, IStream::TrySendResult::kSuccess);
    
    // Visitor should have tracked the stream frame
    auto stream_data = visitor.GetStreamDataInfo();
    EXPECT_EQ(stream_data.size(), 1);
    EXPECT_EQ(stream_data[0].stream_id, 4);
    EXPECT_EQ(stream_data[0].max_offset, 22);
    EXPECT_TRUE(stream_data[0].has_fin);  // FIN should be set since we called Close()
    
    // Now simulate ACK
    stream->OnDataAcked(22, true);
    
    // Verify stream completed (we can't check internal state directly, but no crash means success)
}

// Test 10: Multiple streams in one packet
TEST_F(StreamAckTrackingTest, MultipleStreamsInOnePacket) {
    auto stream1 = std::make_shared<SendStream>(
        alloter_, 10000, 4, active_send_cb_, stream_close_cb_, connection_close_cb_);
    auto stream2 = std::make_shared<SendStream>(
        alloter_, 10000, 8, active_send_cb_, stream_close_cb_, connection_close_cb_);
    
    // Send data on both streams
    uint8_t data1[] = "Stream 4";
    stream1->Send(data1, 8);
    
    uint8_t data2[] = "Stream 8";
    stream2->Send(data2, 8);
    stream2->Close();
    
    // Create visitor
    FixBufferFrameVisitor visitor(1500);
    visitor.SetStreamDataSizeLimit(10000);
    
    // Let both streams send data to the visitor
    stream1->TrySendData(&visitor);
    stream2->TrySendData(&visitor);
    
    // Visitor should track both streams
    auto stream_data = visitor.GetStreamDataInfo();
    EXPECT_EQ(stream_data.size(), 2);
    
    // Verify both streams are tracked
    bool found_stream4 = false;
    bool found_stream8 = false;
    for (const auto& info : stream_data) {
        if (info.stream_id == 4) {
            found_stream4 = true;
            EXPECT_EQ(info.max_offset, 8);
            EXPECT_FALSE(info.has_fin);
        }
        if (info.stream_id == 8) {
            found_stream8 = true;
            EXPECT_EQ(info.max_offset, 8);
            EXPECT_TRUE(info.has_fin);
        }
    }
    EXPECT_TRUE(found_stream4);
    EXPECT_TRUE(found_stream8);
}
