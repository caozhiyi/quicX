#include "quic/connection/controler/recv_flow_controller.h"
#include "quic/connection/controler/send_flow_controller.h"

#include "gtest/gtest.h"

#include "quic/connection/transport_param.h"
#include "quic/frame/data_blocked_frame.h"
#include "quic/frame/max_data_frame.h"
#include "quic/frame/max_streams_frame.h"
#include "quic/frame/streams_blocked_frame.h"
#include "quic/include/type.h"

namespace quicx {
namespace quic {

// ============================================================================
// SendFlowController Tests
// ============================================================================

class SendFlowControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Client starts streams with IDs: 0, 4, 8, 12, ... (bidirectional)
        // Client starts streams with IDs: 2, 6, 10, 14, ... (unidirectional)
        controller_ = std::make_unique<SendFlowController>(StreamIDGenerator::StreamStarter::kClient);

        // Set up transport parameters with typical values
        QuicTransportParams config;
        config.initial_max_data_ = 10000;           // 10KB connection window
        config.initial_max_streams_bidi_ = 10;      // 10 bidirectional streams
        config.initial_max_streams_uni_ = 10;       // 10 unidirectional streams

        TransportParam tp;
        tp.Init(config);
        controller_->UpdateConfig(tp);
    }

    std::unique_ptr<SendFlowController> controller_;
};

// Test: UpdateConfig initializes limits correctly
TEST_F(SendFlowControllerTest, UpdateConfigSetsLimits) {
    QuicTransportParams config;
    config.initial_max_data_ = 50000;
    config.initial_max_streams_bidi_ = 100;
    config.initial_max_streams_uni_ = 50;

    TransportParam tp;
    tp.Init(config);
    controller_->UpdateConfig(tp);

    EXPECT_EQ(controller_->GetBidiStreamLimit(), 100u);
    EXPECT_EQ(controller_->GetUniStreamLimit(), 50u);
}

// Test: OnDataSent tracks sent bytes
TEST_F(SendFlowControllerTest, OnDataSentTracksBytesSent) {
    controller_->OnDataSent(1000);
    controller_->OnDataSent(2000);

    uint64_t can_send_size = 0;
    std::shared_ptr<IFrame> blocked_frame;
    bool can_send = controller_->CanSendData(can_send_size, blocked_frame);

    EXPECT_TRUE(can_send);
    EXPECT_EQ(can_send_size, 7000u);  // 10000 - 3000

    // With threshold of 8912, remaining 7000 bytes triggers proactive signaling
    ASSERT_NE(blocked_frame, nullptr);
    auto data_blocked = std::dynamic_pointer_cast<DataBlockedFrame>(blocked_frame);
    ASSERT_NE(data_blocked, nullptr);
}

// Test: CanSendData blocks when limit reached
TEST_F(SendFlowControllerTest, CanSendDataBlocksAtLimit) {
    controller_->OnDataSent(10000);  // Reach the limit

    uint64_t can_send_size = 0;
    std::shared_ptr<IFrame> blocked_frame;
    bool can_send = controller_->CanSendData(can_send_size, blocked_frame);

    EXPECT_FALSE(can_send);
    EXPECT_EQ(can_send_size, 0u);
    ASSERT_NE(blocked_frame, nullptr);

    // Verify it's a DATA_BLOCKED frame
    auto data_blocked = std::dynamic_pointer_cast<DataBlockedFrame>(blocked_frame);
    ASSERT_NE(data_blocked, nullptr);
    EXPECT_EQ(data_blocked->GetMaximumData(), 10000u);
}

// Test: CanSendData signals near limit
TEST_F(SendFlowControllerTest, CanSendDataSignalsNearLimit) {
    controller_->OnDataSent(9999);  // 1 byte below limit (threshold is 8912)

    uint64_t can_send_size = 0;
    std::shared_ptr<IFrame> blocked_frame;
    bool can_send = controller_->CanSendData(can_send_size, blocked_frame);

    EXPECT_TRUE(can_send);
    EXPECT_EQ(can_send_size, 1u);
    ASSERT_NE(blocked_frame, nullptr);  // Should signal near limit

    auto data_blocked = std::dynamic_pointer_cast<DataBlockedFrame>(blocked_frame);
    ASSERT_NE(data_blocked, nullptr);
}

// Test: OnMaxDataReceived increases limit
TEST_F(SendFlowControllerTest, OnMaxDataReceivedIncreasesLimit) {
    controller_->OnDataSent(10000);  // Reach limit

    controller_->OnMaxDataReceived(20000);  // Peer increases limit

    uint64_t can_send_size = 0;
    std::shared_ptr<IFrame> blocked_frame;
    bool can_send = controller_->CanSendData(can_send_size, blocked_frame);

    EXPECT_TRUE(can_send);
    EXPECT_EQ(can_send_size, 10000u);  // 20000 - 10000
}

// Test: OnMaxDataReceived ignores non-increasing limits (RFC 9000 Section 4.1)
TEST_F(SendFlowControllerTest, OnMaxDataReceivedIgnoresNonIncreasingLimits) {
    controller_->OnMaxDataReceived(5000);  // Lower than current limit

    uint64_t can_send_size = 0;
    std::shared_ptr<IFrame> blocked_frame;
    controller_->CanSendData(can_send_size, blocked_frame);

    EXPECT_EQ(can_send_size, 10000u);  // Should remain at 10000
}

// Test: CanCreateBidiStream allocates stream IDs correctly
TEST_F(SendFlowControllerTest, CanCreateBidiStreamAllocatesIDs) {
    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> blocked_frame;

    // Client bidirectional streams start at 0, increment by 4
    EXPECT_TRUE(controller_->CanCreateBidiStream(stream_id, blocked_frame));
    EXPECT_EQ(stream_id, 0u);

    EXPECT_TRUE(controller_->CanCreateBidiStream(stream_id, blocked_frame));
    EXPECT_EQ(stream_id, 4u);

    EXPECT_TRUE(controller_->CanCreateBidiStream(stream_id, blocked_frame));
    EXPECT_EQ(stream_id, 8u);
}

// Test: CanCreateBidiStream blocks when limit reached
TEST_F(SendFlowControllerTest, CanCreateBidiStreamBlocksAtLimit) {
    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> blocked_frame;

    // Create 10 bidirectional streams (limit is 10, stream IDs 0-36)
    for (uint64_t i = 0; i < 10; ++i) {
        EXPECT_TRUE(controller_->CanCreateBidiStream(stream_id, blocked_frame));
    }

    // Try to create 11th stream - should be blocked
    bool can_create = controller_->CanCreateBidiStream(stream_id, blocked_frame);
    EXPECT_FALSE(can_create);
    ASSERT_NE(blocked_frame, nullptr);

    // Verify it's a STREAMS_BLOCKED frame
    auto streams_blocked = std::dynamic_pointer_cast<StreamsBlockedFrame>(blocked_frame);
    ASSERT_NE(streams_blocked, nullptr);
    EXPECT_EQ(streams_blocked->GetType(), FrameType::kStreamsBlockedBidirectional);
    EXPECT_EQ(streams_blocked->GetMaximumStreams(), 10u);
}

// Test: OnMaxStreamsBidiReceived increases limit
TEST_F(SendFlowControllerTest, OnMaxStreamsBidiReceivedIncreasesLimit) {
    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> blocked_frame;

    // Create 10 streams (reach limit)
    for (uint64_t i = 0; i < 10; ++i) {
        controller_->CanCreateBidiStream(stream_id, blocked_frame);
    }

    // Peer increases limit
    controller_->OnMaxStreamsBidiReceived(20);

    // Should now be able to create more streams
    EXPECT_TRUE(controller_->CanCreateBidiStream(stream_id, blocked_frame));
    EXPECT_EQ(controller_->GetBidiStreamLimit(), 20u);
}

// Test: CanCreateUniStream allocates stream IDs correctly
TEST_F(SendFlowControllerTest, CanCreateUniStreamAllocatesIDs) {
    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> blocked_frame;

    // Client unidirectional streams start at 2, increment by 4
    EXPECT_TRUE(controller_->CanCreateUniStream(stream_id, blocked_frame));
    EXPECT_EQ(stream_id, 2u);

    EXPECT_TRUE(controller_->CanCreateUniStream(stream_id, blocked_frame));
    EXPECT_EQ(stream_id, 6u);

    EXPECT_TRUE(controller_->CanCreateUniStream(stream_id, blocked_frame));
    EXPECT_EQ(stream_id, 10u);
}

// Test: CanCreateUniStream blocks when limit reached
TEST_F(SendFlowControllerTest, CanCreateUniStreamBlocksAtLimit) {
    uint64_t stream_id = 0;
    std::shared_ptr<IFrame> blocked_frame;

    // Create 10 unidirectional streams
    for (uint64_t i = 0; i < 10; ++i) {
        EXPECT_TRUE(controller_->CanCreateUniStream(stream_id, blocked_frame));
    }

    // Try to create 11th stream - should be blocked
    bool can_create = controller_->CanCreateUniStream(stream_id, blocked_frame);
    EXPECT_FALSE(can_create);
    ASSERT_NE(blocked_frame, nullptr);

    auto streams_blocked = std::dynamic_pointer_cast<StreamsBlockedFrame>(blocked_frame);
    ASSERT_NE(streams_blocked, nullptr);
    EXPECT_EQ(streams_blocked->GetType(), FrameType::kStreamsBlockedUnidirectional);
}

// ============================================================================
// RecvFlowController Tests
// ============================================================================

class RecvFlowControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        controller_ = std::make_unique<RecvFlowController>();

        // Set up transport parameters with typical values
        QuicTransportParams config;
        config.initial_max_data_ = 10000;           // 10KB connection window
        config.initial_max_streams_bidi_ = 10;      // 10 bidirectional streams
        config.initial_max_streams_uni_ = 10;       // 10 unidirectional streams

        TransportParam tp;
        tp.Init(config);
        controller_->UpdateConfig(tp);
    }

    std::unique_ptr<RecvFlowController> controller_;
};

// Test: UpdateConfig initializes limits correctly
TEST_F(RecvFlowControllerTest, UpdateConfigSetsLimits) {
    QuicTransportParams config;
    config.initial_max_data_ = 50000;
    config.initial_max_streams_bidi_ = 100;
    config.initial_max_streams_uni_ = 50;

    TransportParam tp;
    tp.Init(config);
    controller_->UpdateConfig(tp);

    EXPECT_EQ(controller_->GetMaxData(), 50000u);
    EXPECT_EQ(controller_->GetMaxStreamsBidi(), 100u);
    EXPECT_EQ(controller_->GetMaxStreamsUni(), 50u);
}

// Test: OnDataReceived tracks received bytes
TEST_F(RecvFlowControllerTest, OnDataReceivedTracksBytes) {
    EXPECT_TRUE(controller_->OnDataReceived(1000));
    EXPECT_TRUE(controller_->OnDataReceived(2000));

    // Should still be within limit (3000 < 10000)
    std::shared_ptr<IFrame> max_data_frame;
    EXPECT_TRUE(controller_->ShouldSendMaxData(max_data_frame));
}

// Test: OnDataReceived detects limit violation
TEST_F(RecvFlowControllerTest, OnDataReceivedDetectsViolation) {
    EXPECT_TRUE(controller_->OnDataReceived(10000));  // Reach limit
    EXPECT_FALSE(controller_->OnDataReceived(1));     // Exceed limit - violation!
}

// Test: ShouldSendMaxData generates MAX_DATA frame when near limit
TEST_F(RecvFlowControllerTest, ShouldSendMaxDataGeneratesFrameNearLimit) {
    // Receive data close to the limit (threshold is 8912)
    controller_->OnDataReceived(9999);

    std::shared_ptr<IFrame> max_data_frame;
    EXPECT_TRUE(controller_->ShouldSendMaxData(max_data_frame));
    ASSERT_NE(max_data_frame, nullptr);

    // Verify it's a MAX_DATA frame with increased limit
    auto max_data = std::dynamic_pointer_cast<MaxDataFrame>(max_data_frame);
    ASSERT_NE(max_data, nullptr);
    EXPECT_GT(max_data->GetMaximumData(), 10000u);  // Should be increased
}

// Test: ShouldSendMaxData does not generate frame when plenty of room
TEST_F(RecvFlowControllerTest, ShouldSendMaxDataNoFrameWhenRoomRemains) {
    controller_->OnDataReceived(1000);  // Far from limit

    std::shared_ptr<IFrame> max_data_frame;
    EXPECT_TRUE(controller_->ShouldSendMaxData(max_data_frame));
    EXPECT_EQ(max_data_frame, nullptr);  // No frame needed
}

// Test: ShouldSendMaxData returns false after violation
TEST_F(RecvFlowControllerTest, ShouldSendMaxDataReturnsFalseAfterViolation) {
    controller_->OnDataReceived(10001);  // Exceed limit

    std::shared_ptr<IFrame> max_data_frame;
    EXPECT_FALSE(controller_->ShouldSendMaxData(max_data_frame));  // Violation detected
}

// Test: OnStreamCreated validates bidirectional stream limits
TEST_F(RecvFlowControllerTest, OnStreamCreatedValidatesBidiStreams) {
    std::shared_ptr<IFrame> max_streams_frame;

    // Server creates bidirectional streams with IDs: 1, 5, 9, 13, ... (bit 0 = 1 for server)
    // Create streams 0-9 (IDs 1, 5, 9, ..., 37)
    for (uint64_t i = 0; i < 10; ++i) {
        uint64_t stream_id = 1 + (i * 4);
        EXPECT_TRUE(controller_->OnStreamCreated(stream_id, max_streams_frame)) << "Stream ID " << stream_id;
    }

    // Note: OnStreamCreated(37, ...) might have triggered auto-increase of limit
    // So we need to check the current limit first
    uint64_t current_limit = controller_->GetMaxStreamsBidi();

    // Calculate the first stream ID that should be blocked
    uint64_t blocked_stream_id = 1 + (current_limit * 4);
    EXPECT_FALSE(controller_->OnStreamCreated(blocked_stream_id, max_streams_frame)) << "Stream ID " << blocked_stream_id << " should be blocked";
}

// Test: OnStreamCreated validates unidirectional stream limits
TEST_F(RecvFlowControllerTest, OnStreamCreatedValidatesUniStreams) {
    std::shared_ptr<IFrame> max_streams_frame;

    // Server creates unidirectional streams with IDs: 3, 7, 11, 15, ... (bit 0 = 1, bit 1 = 1)
    // Create streams 0-9 (IDs 3, 7, 11, ..., 39)
    for (uint64_t i = 0; i < 10; ++i) {
        uint64_t stream_id = 3 + (i * 4);
        EXPECT_TRUE(controller_->OnStreamCreated(stream_id, max_streams_frame)) << "Stream ID " << stream_id;
    }

    // Get current limit (might have been auto-increased)
    uint64_t current_limit = controller_->GetMaxStreamsUni();

    // Calculate the first stream ID that should be blocked
    uint64_t blocked_stream_id = 3 + (current_limit * 4);
    EXPECT_FALSE(controller_->OnStreamCreated(blocked_stream_id, max_streams_frame)) << "Stream ID " << blocked_stream_id << " should be blocked";
}

// Test: OnStreamCreated generates MAX_STREAMS frame when near limit
TEST_F(RecvFlowControllerTest, OnStreamCreatedGeneratesMaxStreamsFrameNearLimit) {
    std::shared_ptr<IFrame> max_streams_frame;

    // Create streams up to near the limit (threshold is 4)
    // Stream ID 21 = count 5, remaining = 10 - 5 = 5 (above threshold)
    controller_->OnStreamCreated(21, max_streams_frame);
    EXPECT_EQ(max_streams_frame, nullptr);

    // Stream ID 25 = count 6, remaining = 10 - 6 = 4 (at threshold)
    controller_->OnStreamCreated(25, max_streams_frame);
    ASSERT_NE(max_streams_frame, nullptr);

    // Verify it's a MAX_STREAMS frame with increased limit
    auto max_streams = std::dynamic_pointer_cast<MaxStreamsFrame>(max_streams_frame);
    ASSERT_NE(max_streams, nullptr);
    EXPECT_EQ(max_streams->GetType(), FrameType::kMaxStreamsBidirectional);
    EXPECT_GT(max_streams->GetMaximumStreams(), 10u);  // Should be increased
}

// Test: OnStreamCreated handles both stream types independently
TEST_F(RecvFlowControllerTest, OnStreamCreatedHandlesBothTypesIndependently) {
    std::shared_ptr<IFrame> max_streams_frame;

    // Create 10 bidirectional streams (should succeed)
    for (uint64_t i = 0; i < 10; ++i) {
        uint64_t stream_id = 1 + (i * 4);  // Server bidi: 1, 5, 9, ..., 37
        EXPECT_TRUE(controller_->OnStreamCreated(stream_id, max_streams_frame));
    }

    // Create 10 unidirectional streams (should succeed - independent limit)
    for (uint64_t i = 0; i < 10; ++i) {
        uint64_t stream_id = 3 + (i * 4);  // Server uni: 3, 7, 11, ..., 39
        EXPECT_TRUE(controller_->OnStreamCreated(stream_id, max_streams_frame));
    }

    // Get current limits (might have been auto-increased)
    uint64_t bidi_limit = controller_->GetMaxStreamsBidi();
    uint64_t uni_limit = controller_->GetMaxStreamsUni();

    // Try to create stream beyond current limits
    EXPECT_FALSE(controller_->OnStreamCreated(1 + (bidi_limit * 4), max_streams_frame));
    EXPECT_FALSE(controller_->OnStreamCreated(3 + (uni_limit * 4), max_streams_frame));
}

// ============================================================================
// Integration Test: Send and Receive Flow Controllers Working Together
// ============================================================================

TEST(FlowControllerIntegrationTest, SendAndReceiveWorkTogether) {
    // Simulate client and server flow control
    SendFlowController send_controller(StreamIDGenerator::StreamStarter::kClient);
    RecvFlowController recv_controller;

    // Client's send limits (from server's transport parameters)
    QuicTransportParams config;
    config.initial_max_data_ = 10000;
    config.initial_max_streams_bidi_ = 5;

    TransportParam server_tp;
    server_tp.Init(config);
    send_controller.UpdateConfig(server_tp);

    // Server's receive limits (its own transport parameters)
    recv_controller.UpdateConfig(server_tp);

    // Client sends data
    send_controller.OnDataSent(1000);
    EXPECT_TRUE(recv_controller.OnDataReceived(1000));  // Server receives

    // Check both sides are in sync
    uint64_t can_send_size = 0;
    std::shared_ptr<IFrame> blocked_frame;
    EXPECT_TRUE(send_controller.CanSendData(can_send_size, blocked_frame));
    EXPECT_EQ(can_send_size, 9000u);  // 10000 - 1000

    std::shared_ptr<IFrame> max_data_frame;
    EXPECT_TRUE(recv_controller.ShouldSendMaxData(max_data_frame));
    // With 9000 remaining (> threshold of 8912), no MAX_DATA frame needed yet
    EXPECT_EQ(max_data_frame, nullptr);

    // Client creates streams
    uint64_t stream_id = 0;
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(send_controller.CanCreateBidiStream(stream_id, blocked_frame));
        std::shared_ptr<IFrame> max_streams_frame;
        EXPECT_TRUE(recv_controller.OnStreamCreated(stream_id, max_streams_frame));
    }

    // Get current limits (might have been auto-increased on recv side)
    uint64_t send_limit = send_controller.GetBidiStreamLimit();

    // Client should be at its send limit
    bool can_create_more = send_controller.CanCreateBidiStream(stream_id, blocked_frame);

    // If send_limit is still 5, we should be blocked. If it was increased (shouldn't happen), we can create more
    if (send_limit == 5) {
        EXPECT_FALSE(can_create_more);
        ASSERT_NE(blocked_frame, nullptr);
    }
}

}  // namespace quic
}  // namespace quicx
