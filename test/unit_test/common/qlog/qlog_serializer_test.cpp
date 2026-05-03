// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <string>

#include "common/qlog/event/connectivity_events.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/serializer/json_seq_serializer.h"
#include "common/qlog/util/qlog_constants.h"

namespace quicx {
namespace common {
namespace {

// Test JsonSeqSerializer creation
TEST(QlogSerializerTest, JsonSeqSerializerCreation) {
    JsonSeqSerializer serializer;
    EXPECT_EQ(QlogFileFormat::kSequential, serializer.GetFormat());
}

// Test trace header serialization
TEST(QlogSerializerTest, SerializeTraceHeaderBasic) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_type = "QUIC";

    QlogConfiguration config;
    config.time_offset = 0;
    config.time_units = "us";

    std::string header =
        serializer.SerializeTraceHeader("test-connection-id", VantagePoint::kServer, common_fields, config);

    // Verify structure: two lines separated by \n
    EXPECT_TRUE(header.find('\n') != std::string::npos);

    // First line: format header
    EXPECT_TRUE(header.find("\"qlog_format\":\"JSON-SEQ\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"qlog_version\":\"0.4\"") != std::string::npos);

    // Second line: trace metadata
    EXPECT_TRUE(header.find("\"title\":\"QuicX server\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"vantage_point\":{") != std::string::npos);
    EXPECT_TRUE(header.find("\"name\":\"server\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"type\":\"server\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"common_fields\":{") != std::string::npos);
    EXPECT_TRUE(header.find("\"protocol_type\":\"QUIC\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"configuration\":{") != std::string::npos);
    EXPECT_TRUE(header.find("\"time_offset\":0") != std::string::npos);
    EXPECT_TRUE(header.find("\"time_units\":\"us\"") != std::string::npos);
    // Note: JSON-SEQ format does not include "events":[] in the trace header
}

// Test trace header with client vantage point
TEST(QlogSerializerTest, SerializeTraceHeaderClient) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    std::string header =
        serializer.SerializeTraceHeader("client-conn-123", VantagePoint::kClient, common_fields, config);

    EXPECT_TRUE(header.find("\"title\":\"QuicX client\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"name\":\"client\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"type\":\"client\"") != std::string::npos);
}

// Test trace header with group_id
TEST(QlogSerializerTest, SerializeTraceHeaderWithGroupId) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_type = "QUIC";
    common_fields.group_id = "test-group-123";

    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader("conn-456", VantagePoint::kServer, common_fields, config);

    EXPECT_TRUE(header.find("\"group_id\":\"test-group-123\"") != std::string::npos);
}

// Test trace header without group_id
TEST(QlogSerializerTest, SerializeTraceHeaderWithoutGroupId) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_type = "QUIC";
    // group_id left empty

    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader("conn-789", VantagePoint::kServer, common_fields, config);

    // Should not include group_id when empty
    EXPECT_TRUE(header.find("\"group_id\"") == std::string::npos);
}

// Test event serialization with PacketSentData
TEST(QlogSerializerTest, SerializePacketSentEvent) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 123456789;
    event.name = "quic:packet_sent";

    auto data = std::make_unique<PacketSentData>();
    data->packet_number = 100;
    data->packet_type = quic::PacketType::k1RttPacketType;
    data->packet_size = 1200;
    data->frames.push_back(quic::FrameType::kStream);
    data->frames.push_back(quic::FrameType::kAck);

    event.data = std::move(data);

    std::string json = serializer.SerializeEvent(event);

    // Verify timestamp (converted to milliseconds with 3 decimal places)
    EXPECT_TRUE(json.find("\"time\":123456.789") != std::string::npos);

    // Verify event name
    EXPECT_TRUE(json.find("\"name\":\"quic:packet_sent\"") != std::string::npos);

    // Verify event data
    EXPECT_TRUE(json.find("\"data\":{") != std::string::npos);
    EXPECT_TRUE(json.find("\"packet_type\":\"1RTT\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"packet_number\":100") != std::string::npos);
}

// Test event serialization with ConnectionStartedData
TEST(QlogSerializerTest, SerializeConnectionStartedEvent) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 0;
    event.name = "quic:connection_started";

    auto data = std::make_unique<ConnectionStartedData>();
    data->src_ip = "192.168.1.100";
    data->src_port = 50123;
    data->dst_ip = "10.0.0.1";
    data->dst_port = 443;
    data->src_cid = "a1b2c3d4";
    data->dst_cid = "e5f6a7b8";

    event.data = std::move(data);

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"time\":0.000") != std::string::npos);
    EXPECT_TRUE(json.find("\"name\":\"quic:connection_started\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"src_ip\":\"192.168.1.100\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"dst_port\":443") != std::string::npos);
}

// Test event serialization without data
TEST(QlogSerializerTest, SerializeEventWithoutData) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;
    event.name = "test:event";
    event.data = nullptr;

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"time\":1.000") != std::string::npos);  // 1000 microseconds = 1.000 milliseconds
    EXPECT_TRUE(json.find("\"name\":\"test:event\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"data\":{}") != std::string::npos);
}

// Test timestamp precision
TEST(QlogSerializerTest, TimestampPrecision) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.name = "test:event";
    event.data = std::make_unique<PacketSentData>();

    // Test various timestamps
    event.time_us = 0;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":0.000") != std::string::npos);

    event.time_us = 1;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":0.001") != std::string::npos);

    event.time_us = 1000;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":1.000") != std::string::npos);

    event.time_us = 1234567;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":1234.567") != std::string::npos);

    event.time_us = 999999;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":999.999") != std::string::npos);
}

// Test JSON-SEQ format compliance
TEST(QlogSerializerTest, JsonSeqFormatCompliance) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader("test-conn", VantagePoint::kServer, common_fields, config);

    // Count newlines (should have 2: one after first line, one after second line)
    size_t newline_count = 0;
    for (char c : header) {
        if (c == '\n') newline_count++;
    }
    EXPECT_EQ(2u, newline_count);

    // Verify both lines end with newline
    EXPECT_EQ('\n', header.back());
}

// Test event JSON line format
TEST(QlogSerializerTest, EventJsonLineFormat) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;
    event.name = "test:event";
    event.data = std::make_unique<PacketSentData>();

    std::string json = serializer.SerializeEvent(event);

    // Should be valid JSON object followed by newline (JSON-SEQ format)
    EXPECT_EQ('{', json.front());
    EXPECT_EQ('\n', json.back());

    // Should have exactly one newline at the end (JSON-SEQ format)
    size_t newline_count = std::count(json.begin(), json.end(), '\n');
    EXPECT_EQ(1u, newline_count);

    // JSON object (without newline) should end with }
    std::string json_without_newline = json.substr(0, json.length() - 1);
    EXPECT_EQ('}', json_without_newline.back());
}

// Test multiple events serialization
TEST(QlogSerializerTest, MultipleEventsSerialization) {
    JsonSeqSerializer serializer;

    std::vector<std::string> serialized_events;

    for (int i = 0; i < 5; i++) {
        QlogEvent event;
        event.time_us = i * 1000;
        event.name = "quic:packet_sent";

        auto data = std::make_unique<PacketSentData>();
        data->packet_number = i;
        data->packet_type = quic::PacketType::k1RttPacketType;
        data->packet_size = 1200;

        event.data = std::move(data);

        serialized_events.push_back(serializer.SerializeEvent(event));
    }

    EXPECT_EQ(5u, serialized_events.size());

    // Verify each event is unique
    for (size_t i = 0; i < serialized_events.size(); i++) {
        EXPECT_TRUE(serialized_events[i].find("\"packet_number\":" + std::to_string(i)) != std::string::npos);
    }
}

// Test VantagePointToString function (used in serializer)
TEST(QlogSerializerTest, VantagePointStrings) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    // Test client
    std::string header = serializer.SerializeTraceHeader("conn1", VantagePoint::kClient, common_fields, config);
    EXPECT_TRUE(header.find("\"client\"") != std::string::npos);

    // Test server
    header = serializer.SerializeTraceHeader("conn2", VantagePoint::kServer, common_fields, config);
    EXPECT_TRUE(header.find("\"server\"") != std::string::npos);

    // Test network
    header = serializer.SerializeTraceHeader("conn3", VantagePoint::kNetwork, common_fields, config);
    EXPECT_TRUE(header.find("\"network\"") != std::string::npos);

    // Test unknown
    header = serializer.SerializeTraceHeader("conn4", VantagePoint::kUnknown, common_fields, config);
    EXPECT_TRUE(header.find("\"unknown\"") != std::string::npos);
}

// Test large packet number
TEST(QlogSerializerTest, LargePacketNumber) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;
    event.name = "quic:packet_sent";

    auto data = std::make_unique<PacketSentData>();
    data->packet_number = UINT64_MAX;
    data->packet_type = quic::PacketType::k1RttPacketType;
    data->packet_size = 1200;

    event.data = std::move(data);

    std::string json = serializer.SerializeEvent(event);

    // Verify large number is serialized correctly
    EXPECT_TRUE(json.find("\"packet_number\":" + std::to_string(UINT64_MAX)) != std::string::npos);
}

// Test empty frames list
TEST(QlogSerializerTest, EmptyFramesList) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;
    event.name = "quic:packet_sent";

    auto data = std::make_unique<PacketSentData>();
    data->packet_number = 1;
    data->packet_type = quic::PacketType::k1RttPacketType;
    data->packet_size = 100;
    // frames list is empty

    event.data = std::move(data);

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"frames\":[]") != std::string::npos);
}

// ====================================================================
// Extended: JSON validity verification for each serialized line
// ====================================================================

// Helper: validate a string is valid JSON by checking balanced braces
// (Lightweight check without external JSON library dependency)
static bool IsBalancedJson(const std::string& json) {
    int braces = 0;
    int brackets = 0;
    bool in_string = false;
    bool escaped = false;

    for (char c : json) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (c == '"') {
            in_string = !in_string;
            continue;
        }
        if (!in_string) {
            if (c == '{') braces++;
            else if (c == '}') braces--;
            else if (c == '[') brackets++;
            else if (c == ']') brackets--;
        }
    }
    return braces == 0 && brackets == 0 && !in_string;
}

// Test: Each header line is independently parseable JSON
TEST(QlogSerializerTest, HeaderLinesAreIndependentJson) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_type = "QUIC";
    common_fields.group_id = "test-group";

    QlogConfiguration config;
    config.time_offset = 0;
    config.time_units = "ms";

    std::string header = serializer.SerializeTraceHeader(
        "conn-json-test", VantagePoint::kServer, common_fields, config);

    // Split into lines
    std::istringstream stream(header);
    std::string line;
    int line_num = 0;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        line_num++;

        // Each line must start with '{' and end with '}'
        EXPECT_EQ('{', line.front())
            << "Header line " << line_num << " should start with '{': " << line;
        EXPECT_EQ('}', line.back())
            << "Header line " << line_num << " should end with '}': " << line;

        // Each line must be balanced JSON
        EXPECT_TRUE(IsBalancedJson(line))
            << "Header line " << line_num << " is not balanced JSON: " << line;
    }

    // Should have exactly 2 header lines
    EXPECT_EQ(2, line_num) << "Header should have exactly 2 lines (format + trace metadata)";
}

// Test: Event line is independently parseable JSON
TEST(QlogSerializerTest, EventLineIsIndependentJson) {
    JsonSeqSerializer serializer;

    // Test with various event types
    struct TestCase {
        std::string event_name;
        std::unique_ptr<EventData> data;
    };

    // PacketSentData
    {
        auto data = std::make_unique<PacketSentData>();
        data->packet_number = 42;
        data->packet_type = quic::PacketType::k1RttPacketType;
        data->packet_size = 1200;
        data->frames.push_back(quic::FrameType::kStream);

        QlogEvent event;
        event.time_us = 100000;
        event.name = "quic:packet_sent";
        event.data = std::move(data);

        std::string json = serializer.SerializeEvent(event);
        std::string json_trimmed = json.substr(0, json.size() - 1);  // remove trailing \n

        EXPECT_EQ('{', json_trimmed.front());
        EXPECT_EQ('}', json_trimmed.back());
        EXPECT_TRUE(IsBalancedJson(json_trimmed))
            << "PacketSentData event is not balanced JSON: " << json_trimmed;
    }

    // ConnectionStartedData
    {
        auto data = std::make_unique<ConnectionStartedData>();
        data->src_ip = "192.168.1.1";
        data->src_port = 443;
        data->dst_ip = "10.0.0.1";
        data->dst_port = 8080;

        QlogEvent event;
        event.time_us = 0;
        event.name = "quic:connection_started";
        event.data = std::move(data);

        std::string json = serializer.SerializeEvent(event);
        std::string json_trimmed = json.substr(0, json.size() - 1);

        EXPECT_TRUE(IsBalancedJson(json_trimmed))
            << "ConnectionStartedData event is not balanced JSON: " << json_trimmed;
    }

    // RecoveryMetricsData
    {
        auto data = std::make_unique<RecoveryMetricsData>();
        data->min_rtt_us = 5000;
        data->cwnd_bytes = 14520;
        data->ssthresh = 20000;
        data->pacing_rate_bps = 1000000;

        QlogEvent event;
        event.time_us = 50000;
        event.name = "recovery:metrics_updated";
        event.data = std::move(data);

        std::string json = serializer.SerializeEvent(event);
        std::string json_trimmed = json.substr(0, json.size() - 1);

        EXPECT_TRUE(IsBalancedJson(json_trimmed))
            << "RecoveryMetricsData event is not balanced JSON: " << json_trimmed;
    }
}

// Test: Header first line contains required qlog_format and qlog_version
TEST(QlogSerializerTest, HeaderFirstLineRequiredFields) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader(
        "conn-hdr", VantagePoint::kClient, common_fields, config);

    // Extract first line
    size_t first_newline = header.find('\n');
    ASSERT_NE(std::string::npos, first_newline);
    std::string first_line = header.substr(0, first_newline);

    // Required fields per qlog spec
    EXPECT_TRUE(first_line.find("\"qlog_format\"") != std::string::npos)
        << "First line must contain qlog_format: " << first_line;
    EXPECT_TRUE(first_line.find("\"qlog_version\"") != std::string::npos)
        << "First line must contain qlog_version: " << first_line;
}

// Test: Header second line contains required vantage_point, common_fields, configuration
TEST(QlogSerializerTest, HeaderSecondLineRequiredFields) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_type = "QUIC";

    QlogConfiguration config;
    config.time_offset = 0;
    config.time_units = "ms";

    std::string header = serializer.SerializeTraceHeader(
        "conn-meta", VantagePoint::kServer, common_fields, config);

    // Extract second line
    size_t first_newline = header.find('\n');
    ASSERT_NE(std::string::npos, first_newline);
    std::string second_line = header.substr(first_newline + 1);
    // Remove trailing newline
    if (!second_line.empty() && second_line.back() == '\n') {
        second_line.pop_back();
    }

    // Required fields in trace metadata
    EXPECT_TRUE(second_line.find("\"vantage_point\"") != std::string::npos)
        << "Second line must contain vantage_point: " << second_line;
    EXPECT_TRUE(second_line.find("\"common_fields\"") != std::string::npos)
        << "Second line must contain common_fields: " << second_line;
    EXPECT_TRUE(second_line.find("\"configuration\"") != std::string::npos)
        << "Second line must contain configuration: " << second_line;
}

// Test: Event contains required fields: time, name, data
TEST(QlogSerializerTest, EventContainsRequiredFields) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 5000;
    event.name = "quic:test_event";
    event.data = std::make_unique<PacketSentData>();

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"time\":") != std::string::npos)
        << "Event must contain time field: " << json;
    EXPECT_TRUE(json.find("\"name\":") != std::string::npos)
        << "Event must contain name field: " << json;
    EXPECT_TRUE(json.find("\"data\":") != std::string::npos)
        << "Event must contain data field: " << json;
}

// Test: Timestamp monotonicity across sequential events
TEST(QlogSerializerTest, TimestampMonotonicity) {
    JsonSeqSerializer serializer;

    std::vector<double> timestamps;

    for (uint64_t i = 0; i < 10; i++) {
        QlogEvent event;
        event.time_us = i * 1000;  // 0, 1000, 2000, ...
        event.name = "test:event";
        event.data = std::make_unique<PacketSentData>();

        std::string json = serializer.SerializeEvent(event);

        // Extract time value: find "time": and parse the number
        size_t time_pos = json.find("\"time\":");
        ASSERT_NE(std::string::npos, time_pos);
        size_t value_start = time_pos + 7;  // length of "\"time\":"
        size_t value_end = json.find(',', value_start);
        std::string time_str = json.substr(value_start, value_end - value_start);
        double time_val = std::stod(time_str);

        timestamps.push_back(time_val);
    }

    // Verify monotonic increase
    for (size_t i = 1; i < timestamps.size(); i++) {
        EXPECT_GE(timestamps[i], timestamps[i - 1])
            << "Timestamps should be monotonically increasing: "
            << timestamps[i - 1] << " -> " << timestamps[i];
    }
}

// Test: Event with group_id includes group_id field
TEST(QlogSerializerTest, EventWithGroupId) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;
    event.name = "quic:test";
    event.data = std::make_unique<PacketSentData>();
    event.group_id = "connection-group-42";

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"group_id\":\"connection-group-42\"") != std::string::npos)
        << "Event with group_id should include group_id field: " << json;
}

// Test: Event without group_id does NOT include group_id field
TEST(QlogSerializerTest, EventWithoutGroupId) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;
    event.name = "quic:test";
    event.data = std::make_unique<PacketSentData>();
    // group_id is empty by default

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"group_id\"") == std::string::npos)
        << "Event without group_id should not include group_id field: " << json;
}

// Test: Full qlog output (header + events) forms valid JSON-SEQ
TEST(QlogSerializerTest, FullQlogOutputIsValidJsonSeq) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_type = "QUIC";
    QlogConfiguration config;
    config.time_offset = 0;
    config.time_units = "ms";

    // Generate complete output
    std::string output = serializer.SerializeTraceHeader(
        "conn-full", VantagePoint::kClient, common_fields, config);

    // Add events
    for (int i = 0; i < 5; i++) {
        QlogEvent event;
        event.time_us = i * 1000;
        event.name = "quic:packet_sent";
        auto data = std::make_unique<PacketSentData>();
        data->packet_number = i;
        data->packet_type = quic::PacketType::k1RttPacketType;
        data->packet_size = 1200;
        event.data = std::move(data);
        output += serializer.SerializeEvent(event);
    }

    // Split into lines and validate each
    std::istringstream stream(output);
    std::string line;
    int line_count = 0;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        line_count++;

        // Each line must be balanced JSON
        EXPECT_TRUE(IsBalancedJson(line))
            << "Line " << line_count << " is not balanced JSON: " << line;
    }

    // 2 header lines + 5 event lines = 7 total
    EXPECT_EQ(7, line_count);
}

}  // namespace
}  // namespace common
}  // namespace quicx
