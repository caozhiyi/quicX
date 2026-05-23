// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>

#include "common/qlog/qlog.h"
#include "common/qlog/qlog_manager.h"
#include "common/qlog/qlog_trace.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/connectivity_events.h"
#include "common/qlog/util/qlog_constants.h"

namespace quicx {
namespace common {
namespace {

namespace fs = std::filesystem;

// File extension produced by JsonSeqSerializer + AsyncWriter (JSON-SEQ /
// draft-02). Tests in this file always read freshly produced files.
constexpr const char* kQlogFileExt = ".sqlog";

// Helper: strip the leading JSON-SEQ Record Separator (0x1E) and any leading
// whitespace before parsing/inspecting a JSON record.
static std::string StripRsPrefix(const std::string& line) {
    size_t i = 0;
    while (i < line.size() && (line[i] == kJsonSeqRecordSeparator || line[i] == ' ' || line[i] == '\t')) {
        ++i;
    }
    return line.substr(i);
}

// Helper: wait for AsyncWriter to flush events to file
static void WaitForFileContent(const std::string& dir, int expected_min_lines, int max_wait_ms = 3000) {
    auto start = std::chrono::steady_clock::now();
    while (true) {
        // Count total non-empty lines across all qlog files in the directory.
        int total_lines = 0;
        if (fs::exists(dir)) {
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (entry.path().extension() == kQlogFileExt) {
                    std::ifstream file(entry.path());
                    std::string line;
                    while (std::getline(file, line)) {
                        if (!line.empty()) total_lines++;
                    }
                }
            }
        }
        if (total_lines >= expected_min_lines) break;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > max_wait_ms) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// Helper: read all (RS-stripped) lines from all qlog files in directory
static std::vector<std::string> ReadAllQlogLines(const std::string& dir) {
    std::vector<std::string> lines;
    if (!fs::exists(dir)) return lines;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == kQlogFileExt) {
            std::ifstream file(entry.path());
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) lines.push_back(StripRsPrefix(line));
            }
        }
    }
    return lines;
}

// Helper: read all (RS-stripped) lines from a specific connection's qlog file
static std::vector<std::string> ReadConnectionQlogLines(const std::string& dir, const std::string& conn_id_prefix) {
    std::vector<std::string> lines;
    if (!fs::exists(dir)) return lines;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == kQlogFileExt &&
            entry.path().filename().string().find(conn_id_prefix) != std::string::npos) {
            std::ifstream file(entry.path());
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) lines.push_back(StripRsPrefix(line));
            }
        }
    }
    return lines;
}

// Helper: lightweight balanced JSON check
static bool IsBalancedJson(const std::string& json) {
    int braces = 0, brackets = 0;
    bool in_string = false, escaped = false;
    for (char c : json) {
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && in_string) { escaped = true; continue; }
        if (c == '"') { in_string = !in_string; continue; }
        if (!in_string) {
            if (c == '{') braces++;
            else if (c == '}') braces--;
            else if (c == '[') brackets++;
            else if (c == ']') brackets--;
        }
    }
    return braces == 0 && brackets == 0 && !in_string;
}

// Test fixture
class QlogE2EOutputTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create unique output directory for each test
        test_dir_ = "./test_qlog_e2e_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());

        QlogConfig config;
        config.enabled = true;
        config.output_dir = test_dir_;
        config.format = QlogFileFormat::kSequential;
        config.flush_interval_ms = 10;
        config.batch_write = true;
        config.sampling_rate = 1.0f;

        QlogManager::Instance().SetConfig(config);
    }

    void TearDown() override {
        QlogManager::Instance().Enable(false);
        // Clean up test directory
        if (fs::exists(test_dir_)) {
            fs::remove_all(test_dir_);
        }
    }

    std::string test_dir_;
};

// Test: Complete connection lifecycle generates valid qlog file
TEST_F(QlogE2EOutputTest, CompleteConnectionLifecycleOutput) {
    std::string conn_id = "e2e-lifecycle";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    // 1. Connection started
    ConnectionStartedData start_data;
    start_data.src_ip = "192.168.1.100";
    start_data.src_port = 50123;
    start_data.dst_ip = "10.0.0.1";
    start_data.dst_port = 443;
    start_data.src_cid = "client001";
    start_data.dst_cid = "server001";
    start_data.protocol = "QUIC";
    start_data.ip_version = "ipv4";
    QLOG_CONNECTION_STARTED(trace, start_data);

    // 2. Packets sent
    for (int i = 0; i < 3; i++) {
        PacketSentData sent;
        sent.packet_number = i;
        sent.packet_type = quic::PacketType::k1RttPacketType;
        sent.packet_size = 1200;
        sent.frames.push_back(quic::FrameType::kStream);
        QLOG_PACKET_SENT(trace, sent);
    }

    // 3. Packets received
    for (int i = 0; i < 2; i++) {
        PacketReceivedData recv;
        recv.packet_number = i;
        recv.packet_type = quic::PacketType::k1RttPacketType;
        recv.packet_size = 800;
        recv.frames.push_back(quic::FrameType::kAck);
        QLOG_PACKET_RECEIVED(trace, recv);
    }

    // 4. ACK
    PacketsAckedData acked;
    acked.ack_ranges.push_back({0, 2});
    acked.ack_delay_us = 5000;
    auto acked_data = std::make_unique<PacketsAckedData>(acked);
    QLOG_EVENT(trace, QlogEvents::kPacketsAcked, std::move(acked_data));

    // 5. Packet lost
    PacketLostData lost;
    lost.packet_number = 10;
    lost.packet_type = quic::PacketType::k1RttPacketType;
    lost.trigger = "time_threshold";
    QLOG_PACKET_LOST(trace, lost);

    // 6. Recovery metrics
    RecoveryMetricsData metrics;
    metrics.min_rtt_us = 10000;
    metrics.smoothed_rtt_us = 12000;
    metrics.latest_rtt_us = 11000;
    metrics.rtt_variance_us = 1000;
    metrics.cwnd_bytes = 14520;
    metrics.bytes_in_flight = 5000;
    QLOG_METRICS_UPDATED(trace, metrics);

    // 7. Congestion state change
    CongestionStateUpdatedData cong;
    cong.old_state = "slow_start";
    cong.new_state = "congestion_avoidance";
    QLOG_CONGESTION_STATE_UPDATED(trace, cong);

    // 8. Connection state change
    ConnectionStateUpdatedData state;
    state.old_state = "connected";
    state.new_state = "closing";
    auto state_data = std::make_unique<ConnectionStateUpdatedData>(state);
    QLOG_EVENT(trace, QlogEvents::kConnectionStateUpdated, std::move(state_data));

    // 9. Connection closed
    ConnectionClosedData close_data;
    close_data.error_code = 0;
    close_data.reason = "Normal close";
    close_data.trigger = "clean";
    QLOG_CONNECTION_CLOSED(trace, close_data);

    // Flush and wait
    QlogManager::Instance().RemoveTrace(conn_id);

    // Expected: 1 trace header line + 11 event lines = 12
    WaitForFileContent(test_dir_, 12);

    // Read and verify the file
    auto lines = ReadConnectionQlogLines(test_dir_, "e2e-life");
    ASSERT_GE(lines.size(), 12u) << "Expected at least 12 lines (1 header + 11 events)";

    // Line 0: trace record (combined LogFile + trace metadata in draft-02)
    EXPECT_TRUE(lines[0].find("\"qlog_format\"") != std::string::npos) << "L0: " << lines[0];
    EXPECT_TRUE(lines[0].find("\"qlog_version\"") != std::string::npos) << "L0: " << lines[0];
    EXPECT_TRUE(lines[0].find("\"vantage_point\"") != std::string::npos) << "L0: " << lines[0];
    EXPECT_TRUE(lines[0].find("\"client\"") != std::string::npos) << "L0: " << lines[0];
    EXPECT_TRUE(lines[0].find("\"common_fields\"") != std::string::npos) << "L0: " << lines[0];

    // Lines 1+: Events - verify each is valid JSON
    for (size_t i = 0; i < lines.size(); i++) {
        EXPECT_TRUE(IsBalancedJson(lines[i]))
            << "Line " << i << " is not valid JSON: " << lines[i];
    }

    // Verify specific events appear in order
    bool found_connection_started = false;
    bool found_packet_sent = false;
    bool found_packet_received = false;
    bool found_packets_acked = false;
    bool found_packet_lost = false;
    bool found_metrics_updated = false;
    bool found_congestion_state = false;
    bool found_connection_state = false;
    bool found_connection_closed = false;

    for (size_t i = 1; i < lines.size(); i++) {
        if (lines[i].find("connection_started") != std::string::npos) found_connection_started = true;
        if (lines[i].find("packet_sent") != std::string::npos) found_packet_sent = true;
        if (lines[i].find("packet_received") != std::string::npos) found_packet_received = true;
        if (lines[i].find("packets_acked") != std::string::npos) found_packets_acked = true;
        if (lines[i].find("packet_lost") != std::string::npos) found_packet_lost = true;
        if (lines[i].find("metrics_updated") != std::string::npos) found_metrics_updated = true;
        if (lines[i].find("congestion_state_updated") != std::string::npos) found_congestion_state = true;
        if (lines[i].find("connection_state_updated") != std::string::npos) found_connection_state = true;
        if (lines[i].find("connection_closed") != std::string::npos) found_connection_closed = true;
    }

    EXPECT_TRUE(found_connection_started) << "Missing connection_started event";
    EXPECT_TRUE(found_packet_sent) << "Missing packet_sent event";
    EXPECT_TRUE(found_packet_received) << "Missing packet_received event";
    EXPECT_TRUE(found_packets_acked) << "Missing packets_acked event";
    EXPECT_TRUE(found_packet_lost) << "Missing packet_lost event";
    EXPECT_TRUE(found_metrics_updated) << "Missing metrics_updated event";
    EXPECT_TRUE(found_congestion_state) << "Missing congestion_state_updated event";
    EXPECT_TRUE(found_connection_state) << "Missing connection_state_updated event";
    EXPECT_TRUE(found_connection_closed) << "Missing connection_closed event";
}

// Test: Events contain correct time, name, data structure
TEST_F(QlogE2EOutputTest, EventFieldsCorrectness) {
    std::string conn_id = "e2e-fields";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kServer);
    ASSERT_NE(nullptr, trace);

    // Send a known event
    PacketSentData sent;
    sent.packet_number = 42;
    sent.packet_type = quic::PacketType::k1RttPacketType;
    sent.packet_size = 1350;
    sent.frames.push_back(quic::FrameType::kStream);
    sent.frames.push_back(quic::FrameType::kAck);
    QLOG_PACKET_SENT(trace, sent);

    QlogManager::Instance().RemoveTrace(conn_id);
    WaitForFileContent(test_dir_, 2);

    auto lines = ReadConnectionQlogLines(test_dir_, "e2e-fiel");
    ASSERT_GE(lines.size(), 2u);

    // Event line (index 1, after the single trace header line)
    const std::string& event_line = lines[1];

    // Must have required fields. Note: per draft-02 `time` is a string-encoded
    // integer (e.g. "12345"), so we look for the field name only.
    EXPECT_TRUE(event_line.find("\"time\":") != std::string::npos)
        << "Event must contain time: " << event_line;
    EXPECT_TRUE(event_line.find("\"name\":\"transport:packet_sent\"") != std::string::npos)
        << "Event must contain correct name: " << event_line;
    EXPECT_TRUE(event_line.find("\"data\":") != std::string::npos)
        << "Event must contain data: " << event_line;

    // Verify data content
    EXPECT_TRUE(event_line.find("\"packet_number\":42") != std::string::npos)
        << "Event data must contain packet_number: " << event_line;
    EXPECT_TRUE(event_line.find("\"packet_type\":\"1RTT\"") != std::string::npos)
        << "Event data must contain packet_type: " << event_line;
    // packet size is now in raw.length per qlog spec.
    EXPECT_TRUE(event_line.find("\"raw\":{\"length\":1350") != std::string::npos)
        << "Event data must contain raw.length: " << event_line;
    EXPECT_TRUE(event_line.find("\"frame_type\":\"stream\"") != std::string::npos)
        << "Event data must contain stream frame: " << event_line;
    EXPECT_TRUE(event_line.find("\"frame_type\":\"ack\"") != std::string::npos)
        << "Event data must contain ack frame: " << event_line;
}

// Test: Multiple connections produce separate files with correct content
TEST_F(QlogE2EOutputTest, MultipleConnectionsSeparateFiles) {
    const int num_conns = 3;
    // Use distinct prefixes (first 8 chars must be different for unique filenames)
    std::vector<std::string> conn_ids = {"alpha001", "bravo002", "charlie3"};
    std::vector<std::shared_ptr<QlogTrace>> traces;

    // Create connections
    for (int i = 0; i < num_conns; i++) {
        auto trace = QlogManager::Instance().CreateTrace(conn_ids[i], VantagePoint::kClient);
        ASSERT_NE(nullptr, trace);
        traces.push_back(trace);
    }

    // Write different events to each connection
    for (int i = 0; i < num_conns; i++) {
        PacketSentData sent;
        sent.packet_number = i * 100;
        sent.packet_type = quic::PacketType::k1RttPacketType;
        sent.packet_size = 1200;
        QLOG_PACKET_SENT(traces[i], sent);
    }

    // Remove all traces
    for (int i = 0; i < num_conns; i++) {
        QlogManager::Instance().RemoveTrace(conn_ids[i]);
    }
    traces.clear();

    // 1 header + 1 event per connection.
    WaitForFileContent(test_dir_, num_conns * 2);

    // Count qlog files
    int file_count = 0;
    if (fs::exists(test_dir_)) {
        for (const auto& entry : fs::directory_iterator(test_dir_)) {
            if (entry.path().extension() == kQlogFileExt) file_count++;
        }
    }
    EXPECT_EQ(num_conns, file_count)
        << "Expected " << num_conns << " qlog files, got " << file_count;

    // Verify each file contains the correct packet_number
    for (int i = 0; i < num_conns; i++) {
        auto lines = ReadAllQlogLines(test_dir_);
        bool found = false;
        for (const auto& line : lines) {
            if (line.find("\"packet_number\":" + std::to_string(i * 100)) != std::string::npos) {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Connection " << i << " event not found in any file";
    }
}

// Test: Event order is preserved in output
TEST_F(QlogE2EOutputTest, EventOrderPreserved) {
    std::string conn_id = "e2e-order";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    // Send events in a specific order
    ConnectionStartedData start;
    start.src_ip = "1.2.3.4";
    start.dst_ip = "5.6.7.8";
    QLOG_CONNECTION_STARTED(trace, start);

    PacketSentData sent;
    sent.packet_number = 0;
    sent.packet_type = quic::PacketType::kInitialPacketType;
    sent.packet_size = 1200;
    QLOG_PACKET_SENT(trace, sent);

    PacketReceivedData recv;
    recv.packet_number = 0;
    recv.packet_type = quic::PacketType::kInitialPacketType;
    recv.packet_size = 800;
    QLOG_PACKET_RECEIVED(trace, recv);

    ConnectionClosedData close_data;
    close_data.error_code = 0;
    close_data.reason = "Done";
    QLOG_CONNECTION_CLOSED(trace, close_data);

    QlogManager::Instance().RemoveTrace(conn_id);
    // 1 header + 4 events
    WaitForFileContent(test_dir_, 5);

    auto lines = ReadConnectionQlogLines(test_dir_, "e2e-orde");
    ASSERT_GE(lines.size(), 5u);

    // Verify order: events at indices 1, 2, 3, 4
    int conn_started_idx = -1, pkt_sent_idx = -1, pkt_recv_idx = -1, conn_closed_idx = -1;

    for (size_t i = 1; i < lines.size(); i++) {
        if (lines[i].find("connection_started") != std::string::npos && conn_started_idx < 0)
            conn_started_idx = static_cast<int>(i);
        if (lines[i].find("packet_sent") != std::string::npos && pkt_sent_idx < 0)
            pkt_sent_idx = static_cast<int>(i);
        if (lines[i].find("packet_received") != std::string::npos && pkt_recv_idx < 0)
            pkt_recv_idx = static_cast<int>(i);
        if (lines[i].find("connection_closed") != std::string::npos && conn_closed_idx < 0)
            conn_closed_idx = static_cast<int>(i);
    }

    EXPECT_GE(conn_started_idx, 0) << "connection_started not found";
    EXPECT_GE(pkt_sent_idx, 0) << "packet_sent not found";
    EXPECT_GE(pkt_recv_idx, 0) << "packet_received not found";
    EXPECT_GE(conn_closed_idx, 0) << "connection_closed not found";

    EXPECT_LT(conn_started_idx, pkt_sent_idx) << "connection_started should come before packet_sent";
    EXPECT_LT(pkt_sent_idx, pkt_recv_idx) << "packet_sent should come before packet_received";
    EXPECT_LT(pkt_recv_idx, conn_closed_idx) << "packet_received should come before connection_closed";
}

// Test: Event whitelist filtering works in output
TEST_F(QlogE2EOutputTest, EventWhitelistFiltering) {
    // Reconfigure with whitelist
    QlogConfig config;
    config.enabled = true;
    config.output_dir = test_dir_;
    config.format = QlogFileFormat::kSequential;
    config.flush_interval_ms = 10;
    config.sampling_rate = 1.0f;
    config.event_whitelist = {QlogEvents::kPacketSent, QlogEvents::kPacketReceived};
    QlogManager::Instance().SetConfig(config);

    std::string conn_id = "e2e-whitelist";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kClient);
    ASSERT_NE(nullptr, trace);

    // Log various events
    ConnectionStartedData start;
    start.src_ip = "1.2.3.4";
    QLOG_CONNECTION_STARTED(trace, start);  // Should be filtered

    PacketSentData sent;
    sent.packet_number = 1;
    sent.packet_type = quic::PacketType::k1RttPacketType;
    sent.packet_size = 1200;
    QLOG_PACKET_SENT(trace, sent);  // Should pass

    RecoveryMetricsData metrics;
    metrics.cwnd_bytes = 14520;
    QLOG_METRICS_UPDATED(trace, metrics);  // Should be filtered

    PacketReceivedData recv;
    recv.packet_number = 1;
    recv.packet_type = quic::PacketType::k1RttPacketType;
    recv.packet_size = 800;
    QLOG_PACKET_RECEIVED(trace, recv);  // Should pass

    // Only 2 events should have been logged
    EXPECT_EQ(2u, trace->GetEventCount());

    QlogManager::Instance().RemoveTrace(conn_id);
    // 1 header + 2 events
    WaitForFileContent(test_dir_, 3);

    auto lines = ReadConnectionQlogLines(test_dir_, "e2e-whit");
    // 1 header + 2 events
    EXPECT_EQ(3u, lines.size()) << "Expected 3 lines (1 header + 2 filtered events)";

    // Verify only allowed events appear (skip line 0 which is the header)
    for (size_t i = 1; i < lines.size(); i++) {
        EXPECT_TRUE(
            lines[i].find("packet_sent") != std::string::npos ||
            lines[i].find("packet_received") != std::string::npos)
            << "Unexpected event in filtered output: " << lines[i];
    }
}

// Test: Large number of events produces complete output
TEST_F(QlogE2EOutputTest, LargeEventVolume) {
    std::string conn_id = "e2e-volume";
    auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kServer);
    ASSERT_NE(nullptr, trace);

    const int num_events = 500;
    for (int i = 0; i < num_events; i++) {
        PacketSentData sent;
        sent.packet_number = i;
        sent.packet_type = quic::PacketType::k1RttPacketType;
        sent.packet_size = 1200;
        QLOG_PACKET_SENT(trace, sent);
    }

    EXPECT_EQ(static_cast<uint64_t>(num_events), trace->GetEventCount());

    QlogManager::Instance().RemoveTrace(conn_id);
    // 1 header + num_events
    WaitForFileContent(test_dir_, num_events + 1, 5000);

    auto lines = ReadConnectionQlogLines(test_dir_, "e2e-volu");
    EXPECT_GE(lines.size(), static_cast<size_t>(num_events + 1))
        << "Expected " << (num_events + 1) << " lines, got " << lines.size();
}

// Test: Vantage point is correctly recorded in file
TEST_F(QlogE2EOutputTest, VantagePointInFile) {
    // Test server vantage point
    {
        std::string conn_id = "e2e-vp-server";
        auto trace = QlogManager::Instance().CreateTrace(conn_id, VantagePoint::kServer);
        ASSERT_NE(nullptr, trace);

        PacketSentData sent;
        sent.packet_number = 1;
        sent.packet_type = quic::PacketType::k1RttPacketType;
        sent.packet_size = 100;
        QLOG_PACKET_SENT(trace, sent);

        QlogManager::Instance().RemoveTrace(conn_id);
    }

    // 1 header + 1 event
    WaitForFileContent(test_dir_, 2);

    auto lines = ReadConnectionQlogLines(test_dir_, "e2e-vp-s");
    ASSERT_GE(lines.size(), 1u);

    // Trace metadata is on line 0 (combined LogFile + trace per draft-02).
    EXPECT_TRUE(lines[0].find("\"type\":\"server\"") != std::string::npos)
        << "Vantage point should be server: " << lines[0];
}

}  // namespace
}  // namespace common
}  // namespace quicx
