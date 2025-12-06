// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <chrono>

#include "common/qlog/qlog_trace.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/connectivity_events.h"
#include "common/qlog/writer/async_writer.h"

namespace quicx {
namespace common {
namespace {

// Helper function to create a basic config
QlogConfig CreateTestConfig() {
    QlogConfig config;
    config.enabled = true;
    config.output_dir = "./test_qlogs";
    config.format = QlogFileFormat::kSequential;
    return config;
}

// Helper function to create a trace with a writer set up
std::unique_ptr<QlogTrace> CreateTraceWithWriter(const std::string& conn_id, VantagePoint vantage_point,
                                                   std::shared_ptr<AsyncWriter>& writer_out) {
    QlogConfig config = CreateTestConfig();
    auto trace = std::make_unique<QlogTrace>(conn_id, vantage_point, config);
    // Create writer but don't start it (avoid thread issues in tests)
    auto writer = std::make_shared<AsyncWriter>(config);
    trace->SetWriter(writer.get());
    writer_out = writer;
    return trace;
}

// Test QlogTrace creation
TEST(QlogTraceTest, Creation) {
    QlogConfig config = CreateTestConfig();

    QlogTrace trace("test-connection-id", VantagePoint::kServer, config);

    EXPECT_EQ("test-connection-id", trace.GetConnectionId());
    EXPECT_EQ(VantagePoint::kServer, trace.GetVantagePoint());
    EXPECT_EQ(0u, trace.GetEventCount());
}

// Test QlogTrace with client vantage point
TEST(QlogTraceTest, ClientVantagePoint) {
    QlogConfig config = CreateTestConfig();

    QlogTrace trace("client-conn-123", VantagePoint::kClient, config);

    EXPECT_EQ("client-conn-123", trace.GetConnectionId());
    EXPECT_EQ(VantagePoint::kClient, trace.GetVantagePoint());
}

// Test LogPacketSent convenience method
TEST(QlogTraceTest, LogPacketSent) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-1", VantagePoint::kServer, writer);

    PacketSentData data;
    data.packet_number = 100;
    data.packet_type = quic::PacketType::k1RttPacketType;
    data.packet_size = 1200;
    data.frames.push_back(quic::FrameType::kStream);

    uint64_t time_us = 123456;
    trace->LogPacketSent(time_us, data);

    EXPECT_EQ(1u, trace->GetEventCount());
}

// Test LogPacketReceived convenience method
TEST(QlogTraceTest, LogPacketReceived) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-2", VantagePoint::kClient, writer);

    PacketReceivedData data;
    data.packet_number = 200;
    data.packet_type = quic::PacketType::kHandshakePacketType;
    data.packet_size = 800;
    data.frames.push_back(quic::FrameType::kCrypto);

    uint64_t time_us = 234567;
    trace->LogPacketReceived(time_us, data);

    EXPECT_EQ(1u, trace->GetEventCount());
}

// Test LogMetricsUpdated convenience method
TEST(QlogTraceTest, LogMetricsUpdated) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-3", VantagePoint::kServer, writer);

    RecoveryMetricsData data;
    data.min_rtt_us = 10000;
    data.smoothed_rtt_us = 12000;
    data.cwnd_bytes = 14520;
    data.bytes_in_flight = 5000;

    uint64_t time_us = 345678;
    trace->LogMetricsUpdated(time_us, data);

    EXPECT_EQ(1u, trace->GetEventCount());
}

// Test LogConnectionStarted convenience method
TEST(QlogTraceTest, LogConnectionStarted) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-4", VantagePoint::kServer, writer);

    ConnectionStartedData data;
    data.src_ip = "192.168.1.100";
    data.src_port = 50123;
    data.dst_ip = "10.0.0.1";
    data.dst_port = 443;

    uint64_t time_us = 0;
    trace->LogConnectionStarted(time_us, data);

    EXPECT_EQ(1u, trace->GetEventCount());
}

// Test LogConnectionClosed convenience method
TEST(QlogTraceTest, LogConnectionClosed) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-5", VantagePoint::kClient, writer);

    ConnectionClosedData data;
    data.error_code = 0;
    data.reason = "Normal close";
    data.trigger = "clean";

    uint64_t time_us = 999999;
    trace->LogConnectionClosed(time_us, data);

    EXPECT_EQ(1u, trace->GetEventCount());
}

// Test LogEvent generic method
TEST(QlogTraceTest, LogEventGeneric) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-6", VantagePoint::kServer, writer);

    auto data = std::make_unique<PacketSentData>();
    data->packet_number = 1;
    data->packet_type = quic::PacketType::k1RttPacketType;
    data->packet_size = 100;

    uint64_t time_us = 111111;
    trace->LogEvent(time_us, "quic:packet_sent", std::move(data));

    EXPECT_EQ(1u, trace->GetEventCount());
}

// Test multiple events
TEST(QlogTraceTest, MultipleEvents) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-7", VantagePoint::kServer, writer);

    // Log connection started
    ConnectionStartedData start_data;
    start_data.src_ip = "192.168.1.1";
    start_data.dst_ip = "10.0.0.1";
    trace->LogConnectionStarted(0, start_data);

    // Log multiple packet sent
    for (int i = 0; i < 10; i++) {
        PacketSentData packet_data;
        packet_data.packet_number = i;
        packet_data.packet_type = quic::PacketType::k1RttPacketType;
        packet_data.packet_size = 1200;
        trace->LogPacketSent(i * 1000, packet_data);
    }

    // Log connection closed
    ConnectionClosedData close_data;
    close_data.error_code = 0;
    close_data.reason = "Done";
    trace->LogConnectionClosed(20000, close_data);

    EXPECT_EQ(12u, trace->GetEventCount());
}

// Test SetCommonFields
TEST(QlogTraceTest, SetCommonFields) {
    QlogConfig config = CreateTestConfig();
    QlogTrace trace("conn-8", VantagePoint::kServer, config);

    CommonFields fields;
    fields.protocol_type = "QUIC";
    fields.group_id = "test-group-1";

    trace.SetCommonFields(fields);

    // Should not throw or crash
    EXPECT_EQ("conn-8", trace.GetConnectionId());
}

// Test SetConfiguration
TEST(QlogTraceTest, SetConfiguration) {
    QlogConfig config = CreateTestConfig();
    QlogTrace trace("conn-9", VantagePoint::kServer, config);

    QlogConfiguration qlog_config;
    qlog_config.time_offset = 123456;
    qlog_config.time_units = "ms";

    trace.SetConfiguration(qlog_config);

    // Should not throw or crash
    EXPECT_EQ("conn-9", trace.GetConnectionId());
}

// Test event counter increments
TEST(QlogTraceTest, EventCounterIncrements) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-10", VantagePoint::kServer, writer);

    EXPECT_EQ(0u, trace->GetEventCount());

    PacketSentData data;
    data.packet_number = 1;
    data.packet_type = quic::PacketType::k1RttPacketType;
    data.packet_size = 100;

    trace->LogPacketSent(1000, data);
    EXPECT_EQ(1u, trace->GetEventCount());

    trace->LogPacketSent(2000, data);
    EXPECT_EQ(2u, trace->GetEventCount());

    trace->LogPacketSent(3000, data);
    EXPECT_EQ(3u, trace->GetEventCount());
}

// Test with event whitelist filter
TEST(QlogTraceTest, EventWhitelistFilter) {
    std::shared_ptr<AsyncWriter> writer;
    QlogConfig config = CreateTestConfig();
    config.event_whitelist = {"quic:packet_sent", "quic:packet_received"};

    auto trace = std::make_unique<QlogTrace>("conn-11", VantagePoint::kServer, config);
    writer = std::make_shared<AsyncWriter>(config);
    trace->SetWriter(writer.get());

    // These should be logged
    PacketSentData sent_data;
    sent_data.packet_number = 1;
    sent_data.packet_type = quic::PacketType::k1RttPacketType;
    sent_data.packet_size = 100;
    trace->LogPacketSent(1000, sent_data);

    PacketReceivedData recv_data;
    recv_data.packet_number = 2;
    recv_data.packet_type = quic::PacketType::k1RttPacketType;
    recv_data.packet_size = 100;
    trace->LogPacketReceived(2000, recv_data);

    // This should be filtered out (not in whitelist)
    RecoveryMetricsData metrics_data;
    metrics_data.cwnd_bytes = 14520;
    trace->LogMetricsUpdated(3000, metrics_data);

    // Only packet_sent and packet_received should be counted
    EXPECT_EQ(2u, trace->GetEventCount());

    // Cleanup: destroy trace before writer
    trace.reset();
}

// Test with event blacklist filter
TEST(QlogTraceTest, EventBlacklistFilter) {
    std::shared_ptr<AsyncWriter> writer;
    QlogConfig config = CreateTestConfig();
    config.event_blacklist = {"recovery:metrics_updated"};

    auto trace = std::make_unique<QlogTrace>("conn-12", VantagePoint::kServer, config);
    writer = std::make_shared<AsyncWriter>(config);
    trace->SetWriter(writer.get());

    // This should be logged
    PacketSentData sent_data;
    sent_data.packet_number = 1;
    sent_data.packet_type = quic::PacketType::k1RttPacketType;
    sent_data.packet_size = 100;
    trace->LogPacketSent(1000, sent_data);

    // This should be filtered out (in blacklist)
    RecoveryMetricsData metrics_data;
    metrics_data.cwnd_bytes = 14520;
    trace->LogMetricsUpdated(2000, metrics_data);

    // Only packet_sent should be counted
    EXPECT_EQ(1u, trace->GetEventCount());

    // Cleanup: destroy trace before writer
    trace.reset();
}

// Test flush (should not crash)
TEST(QlogTraceTest, FlushWithoutWriter) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-13", VantagePoint::kServer, writer);

    PacketSentData data;
    data.packet_number = 1;
    data.packet_type = quic::PacketType::k1RttPacketType;
    data.packet_size = 100;
    trace->LogPacketSent(1000, data);

    // Flush should not crash
    trace->Flush();

    EXPECT_EQ(1u, trace->GetEventCount());
}

// Test thread safety (basic concurrent logging)
TEST(QlogTraceTest, ConcurrentLogging) {
    QlogConfig config = CreateTestConfig();
    auto trace = std::make_shared<QlogTrace>("conn-14", VantagePoint::kServer, config);
    auto writer = std::make_shared<AsyncWriter>(config);
    trace->SetWriter(writer.get());

    const int num_threads = 4;
    const int events_per_thread = 25;

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; t++) {
        threads.emplace_back([trace, t, events_per_thread]() {
            for (int i = 0; i < events_per_thread; i++) {
                PacketSentData data;
                data.packet_number = t * events_per_thread + i;
                data.packet_type = quic::PacketType::k1RttPacketType;
                data.packet_size = 1200;
                trace->LogPacketSent(i * 1000, data);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(num_threads * events_per_thread, trace->GetEventCount());

    // Cleanup: destroy trace before writer
    trace.reset();
}

// Test with different packet types
TEST(QlogTraceTest, DifferentPacketTypes) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-15", VantagePoint::kServer, writer);

    std::vector<quic::PacketType> packet_types = {
        quic::PacketType::kInitialPacketType,
        quic::PacketType::k0RttPacketType,
        quic::PacketType::kHandshakePacketType,
        quic::PacketType::k1RttPacketType,
        quic::PacketType::kRetryPacketType,
    };

    for (size_t i = 0; i < packet_types.size(); i++) {
        PacketSentData data;
        data.packet_number = i;
        data.packet_type = packet_types[i];
        data.packet_size = 1200;
        trace->LogPacketSent(i * 1000, data);
    }

    EXPECT_EQ(packet_types.size(), trace->GetEventCount());
}

// Test large number of events
TEST(QlogTraceTest, LargeNumberOfEvents) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-16", VantagePoint::kServer, writer);

    const size_t num_events = 1000;

    for (size_t i = 0; i < num_events; i++) {
        PacketSentData data;
        data.packet_number = i;
        data.packet_type = quic::PacketType::k1RttPacketType;
        data.packet_size = 1200;
        trace->LogPacketSent(i, data);
    }

    EXPECT_EQ(num_events, trace->GetEventCount());
}

// Test event with all frame types
TEST(QlogTraceTest, AllFrameTypes) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-17", VantagePoint::kServer, writer);

    PacketSentData data;
    data.packet_number = 1;
    data.packet_type = quic::PacketType::k1RttPacketType;
    data.packet_size = 1200;

    // Add various frame types
    data.frames.push_back(quic::FrameType::kPadding);
    data.frames.push_back(quic::FrameType::kPing);
    data.frames.push_back(quic::FrameType::kAck);
    data.frames.push_back(quic::FrameType::kStream);
    data.frames.push_back(quic::FrameType::kCrypto);
    data.frames.push_back(quic::FrameType::kMaxData);
    data.frames.push_back(quic::FrameType::kConnectionClose);

    trace->LogPacketSent(1000, data);

    EXPECT_EQ(1u, trace->GetEventCount());
}

// Test connection lifecycle events
TEST(QlogTraceTest, ConnectionLifecycle) {
    std::shared_ptr<AsyncWriter> writer;
    auto trace = CreateTraceWithWriter("conn-18", VantagePoint::kServer, writer);

    // Connection started
    ConnectionStartedData start_data;
    start_data.src_ip = "192.168.1.1";
    start_data.dst_ip = "10.0.0.1";
    trace->LogConnectionStarted(0, start_data);
    EXPECT_EQ(1u, trace->GetEventCount());

    // Some data exchange
    for (int i = 0; i < 5; i++) {
        PacketSentData sent;
        sent.packet_number = i * 2;
        sent.packet_type = quic::PacketType::k1RttPacketType;
        sent.packet_size = 1200;
        trace->LogPacketSent(i * 1000, sent);

        PacketReceivedData recv;
        recv.packet_number = i * 2 + 1;
        recv.packet_type = quic::PacketType::k1RttPacketType;
        recv.packet_size = 1200;
        trace->LogPacketReceived(i * 1000 + 500, recv);
    }
    EXPECT_EQ(11u, trace->GetEventCount());

    // Connection closed
    ConnectionClosedData close_data;
    close_data.error_code = 0;
    close_data.reason = "Complete";
    trace->LogConnectionClosed(10000, close_data);
    EXPECT_EQ(12u, trace->GetEventCount());
}

}  // namespace
}  // namespace common
}  // namespace quicx
