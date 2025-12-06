// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "common/qlog/qlog.h"
#include "common/qlog/qlog_manager.h"
#include "common/qlog/qlog_trace.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/connectivity_events.h"

namespace quicx {
namespace common {
namespace {

// Helper function to create a basic config
QlogConfig CreateTestConfig() {
    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs_integration";
    config.format = QlogFileFormat::kSequential;
    return config;
}

// Test fixture for proper setup/teardown
class QlogIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        QlogManager::Instance().SetConfig(CreateTestConfig());
    }

    void TearDown() override {
        QlogManager::Instance().Enable(false);
    }
};

// Test P0: Connection lifecycle events
TEST_F(QlogIntegrationTest, ConnectionLifecycle) {
    std::string conn_id = "test-conn-lifecycle";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    // Log connection_started event
    ConnectionStartedData start_data;
    start_data.src_ip = "192.168.1.100";
    start_data.src_port = 50123;
    start_data.dst_ip = "10.0.0.1";
    start_data.dst_port = 443;
    start_data.src_cid = "client-cid-123";
    start_data.dst_cid = "server-cid-456";
    start_data.protocol = "QUIC";
    start_data.ip_version = "ipv4";

    QLOG_CONNECTION_STARTED(trace, start_data);
    EXPECT_EQ(1u, trace->GetEventCount());

    // Log connection_closed event
    ConnectionClosedData close_data;
    close_data.error_code = 0;
    close_data.reason = "Normal close";
    close_data.trigger = "clean";

    QLOG_CONNECTION_CLOSED(trace, close_data);
    EXPECT_EQ(2u, trace->GetEventCount());

    // Cleanup
    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test P0: Packet transmission events
TEST_F(QlogIntegrationTest, PacketTransmission) {
    std::string conn_id = "test-conn-packets";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kServer);
    ASSERT_NE(nullptr, trace);

    // Log packet_sent event
    PacketSentData sent_data;
    sent_data.packet_number = 1;
    sent_data.packet_type = quic::PacketType::k1RttPacketType;
    sent_data.packet_size = 1200;
    sent_data.frames.push_back(quic::FrameType::kStream);
    sent_data.frames.push_back(quic::FrameType::kAck);

    QLOG_PACKET_SENT(trace, sent_data);
    EXPECT_EQ(1u, trace->GetEventCount());

    // Log packet_received event
    PacketReceivedData recv_data;
    recv_data.packet_number = 2;
    recv_data.packet_type = quic::PacketType::k1RttPacketType;
    recv_data.packet_size = 800;
    recv_data.frames.push_back(quic::FrameType::kAck);

    QLOG_PACKET_RECEIVED(trace, recv_data);
    EXPECT_EQ(2u, trace->GetEventCount());

    // Cleanup
    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test P1: Loss detection events
TEST_F(QlogIntegrationTest, LossDetection) {
    std::string conn_id = "test-conn-loss";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    // Log packet_lost event with packet_threshold trigger
    PacketLostData lost_data;
    lost_data.packet_number = 10;
    lost_data.packet_type = quic::PacketType::k1RttPacketType;
    lost_data.trigger = "packet_threshold";

    QLOG_PACKET_LOST(trace, lost_data);
    EXPECT_EQ(1u, trace->GetEventCount());

    // Log packet_lost event with time_threshold trigger
    PacketLostData lost_data2;
    lost_data2.packet_number = 15;
    lost_data2.packet_type = quic::PacketType::k1RttPacketType;
    lost_data2.trigger = "time_threshold";

    QLOG_PACKET_LOST(trace, lost_data2);
    EXPECT_EQ(2u, trace->GetEventCount());

    // Cleanup
    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test P1: ACK processing events
TEST_F(QlogIntegrationTest, AckProcessing) {
    std::string conn_id = "test-conn-ack";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kServer);
    ASSERT_NE(nullptr, trace);

    // Log packets_acked event
    PacketsAckedData acked_data;

    // Add ACK ranges
    PacketsAckedData::AckRange range1;
    range1.start = 1;
    range1.end = 10;
    acked_data.ack_ranges.push_back(range1);

    PacketsAckedData::AckRange range2;
    range2.start = 15;
    range2.end = 20;
    acked_data.ack_ranges.push_back(range2);

    acked_data.ack_delay_us = 5000;

    auto event_data = std::make_unique<PacketsAckedData>(acked_data);
    QLOG_EVENT(trace, QlogEvents::kPacketsAcked, std::move(event_data));
    EXPECT_EQ(1u, trace->GetEventCount());

    // Cleanup
    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test P1: Connection state transitions
TEST_F(QlogIntegrationTest, ConnectionStateTransitions) {
    std::string conn_id = "test-conn-state";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    // Log state transition: handshake -> connected
    ConnectionStateUpdatedData state_data1;
    state_data1.old_state = "handshake";
    state_data1.new_state = "connected";

    auto event1 = std::make_unique<ConnectionStateUpdatedData>(state_data1);
    QLOG_EVENT(trace, QlogEvents::kConnectionStateUpdated, std::move(event1));
    EXPECT_EQ(1u, trace->GetEventCount());

    // Log state transition: connected -> closing
    ConnectionStateUpdatedData state_data2;
    state_data2.old_state = "connected";
    state_data2.new_state = "closing";

    auto event2 = std::make_unique<ConnectionStateUpdatedData>(state_data2);
    QLOG_EVENT(trace, QlogEvents::kConnectionStateUpdated, std::move(event2));
    EXPECT_EQ(2u, trace->GetEventCount());

    // Cleanup
    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test P2: Recovery metrics events
TEST_F(QlogIntegrationTest, RecoveryMetrics) {
    std::string conn_id = "test-conn-metrics";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kServer);
    ASSERT_NE(nullptr, trace);

    // Log recovery_metrics_updated event
    RecoveryMetricsData metrics_data;
    metrics_data.min_rtt_us = 10000;
    metrics_data.smoothed_rtt_us = 12000;
    metrics_data.latest_rtt_us = 11500;
    metrics_data.rtt_variance_us = 1000;
    metrics_data.cwnd_bytes = 14520;
    metrics_data.bytes_in_flight = 5000;
    metrics_data.ssthresh = 20000;
    metrics_data.pacing_rate_bps = 10000000;  // 10 Mbps

    QLOG_METRICS_UPDATED(trace, metrics_data);
    EXPECT_EQ(1u, trace->GetEventCount());

    // Cleanup
    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test P2: Congestion state transitions
TEST_F(QlogIntegrationTest, CongestionStateTransitions) {
    std::string conn_id = "test-conn-congestion";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    // Log congestion state transition: slow_start -> congestion_avoidance
    CongestionStateUpdatedData cong_data1;
    cong_data1.old_state = "slow_start";
    cong_data1.new_state = "congestion_avoidance";

    QLOG_CONGESTION_STATE_UPDATED(trace, cong_data1);
    EXPECT_EQ(1u, trace->GetEventCount());

    // Log congestion state transition: congestion_avoidance -> recovery
    CongestionStateUpdatedData cong_data2;
    cong_data2.old_state = "congestion_avoidance";
    cong_data2.new_state = "recovery";

    QLOG_CONGESTION_STATE_UPDATED(trace, cong_data2);
    EXPECT_EQ(2u, trace->GetEventCount());

    // Log congestion state transition: recovery -> congestion_avoidance
    CongestionStateUpdatedData cong_data3;
    cong_data3.old_state = "recovery";
    cong_data3.new_state = "congestion_avoidance";

    QLOG_CONGESTION_STATE_UPDATED(trace, cong_data3);
    EXPECT_EQ(3u, trace->GetEventCount());

    // Cleanup
    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test complete connection flow with all event types
TEST_F(QlogIntegrationTest, CompleteConnectionFlow) {
    std::string conn_id = "test-conn-complete";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    size_t event_count = 0;

    // 1. Connection started
    ConnectionStartedData start_data;
    start_data.src_ip = "10.0.0.1";
    start_data.dst_ip = "192.168.1.1";
    QLOG_CONNECTION_STARTED(trace, start_data);
    EXPECT_EQ(++event_count, trace->GetEventCount());

    // 2. Initial packets sent/received
    for (int i = 0; i < 5; i++) {
        PacketSentData sent;
        sent.packet_number = i * 2;
        sent.packet_type = quic::PacketType::k1RttPacketType;
        sent.packet_size = 1200;
        QLOG_PACKET_SENT(trace, sent);
        event_count++;

        PacketReceivedData recv;
        recv.packet_number = i * 2 + 1;
        recv.packet_type = quic::PacketType::k1RttPacketType;
        recv.packet_size = 800;
        QLOG_PACKET_RECEIVED(trace, recv);
        event_count++;
    }
    EXPECT_EQ(event_count, trace->GetEventCount());

    // 3. Congestion state transition
    CongestionStateUpdatedData cong_data;
    cong_data.old_state = "slow_start";
    cong_data.new_state = "congestion_avoidance";
    QLOG_CONGESTION_STATE_UPDATED(trace, cong_data);
    EXPECT_EQ(++event_count, trace->GetEventCount());

    // 4. Recovery metrics
    RecoveryMetricsData metrics;
    metrics.cwnd_bytes = 14520;
    metrics.smoothed_rtt_us = 12000;
    QLOG_METRICS_UPDATED(trace, metrics);
    EXPECT_EQ(++event_count, trace->GetEventCount());

    // 5. Packet loss
    PacketLostData lost;
    lost.packet_number = 100;
    lost.packet_type = quic::PacketType::k1RttPacketType;
    lost.trigger = "packet_threshold";
    QLOG_PACKET_LOST(trace, lost);
    EXPECT_EQ(++event_count, trace->GetEventCount());

    // 6. State transition
    ConnectionStateUpdatedData state;
    state.old_state = "connected";
    state.new_state = "closing";
    auto state_event = std::make_unique<ConnectionStateUpdatedData>(state);
    QLOG_EVENT(trace, QlogEvents::kConnectionStateUpdated, std::move(state_event));
    EXPECT_EQ(++event_count, trace->GetEventCount());

    // 7. Connection closed
    ConnectionClosedData close_data;
    close_data.error_code = 0;
    close_data.reason = "Complete";
    QLOG_CONNECTION_CLOSED(trace, close_data);
    EXPECT_EQ(++event_count, trace->GetEventCount());

    // Should have logged all events (1 + 10 + 1 + 1 + 1 + 1 + 1 = 16)
    EXPECT_EQ(16u, trace->GetEventCount());

    // Cleanup
    QlogManager::Instance().RemoveTrace(conn_id);
}

// Test qlog disabled state
TEST_F(QlogIntegrationTest, QlogDisabled) {
    // Disable qlog
    QlogManager::Instance().Enable(false);

    std::string conn_id = "test-conn-disabled";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);

    // CreateTrace should return nullptr when qlog is disabled
    EXPECT_EQ(nullptr, trace);
}

// Test multiple concurrent connections
TEST_F(QlogIntegrationTest, MultipleConcurrentConnections) {
    const int num_connections = 5;
    std::vector<std::shared_ptr<QlogTrace>> traces;

    // Create multiple traces
    for (int i = 0; i < num_connections; i++) {
        std::string conn_id = "test-conn-multi-" + std::to_string(i);
        auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
        ASSERT_NE(nullptr, trace);
        traces.push_back(trace);
    }

    // Log events to each trace
    for (int i = 0; i < num_connections; i++) {
        PacketSentData sent;
        sent.packet_number = i;
        sent.packet_type = quic::PacketType::k1RttPacketType;
        sent.packet_size = 1200;
        QLOG_PACKET_SENT(traces[i], sent);

        EXPECT_EQ(1u, traces[i]->GetEventCount());
    }

    // Cleanup - must remove all traces before TearDown destroys the writer
    for (int i = 0; i < num_connections; i++) {
        std::string conn_id = "test-conn-multi-" + std::to_string(i);
        QlogManager::Instance().RemoveTrace(conn_id);
    }
    traces.clear();  // Release all shared_ptrs before TearDown
}

}  // namespace
}  // namespace common
}  // namespace quicx
