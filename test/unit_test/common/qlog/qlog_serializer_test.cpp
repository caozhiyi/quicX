// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <string>

#include "common/qlog/serializer/json_seq_serializer.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/event/connectivity_events.h"
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

    std::string header = serializer.SerializeTraceHeader(
        "test-connection-id",
        VantagePoint::kServer,
        common_fields,
        config
    );

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
    EXPECT_TRUE(header.find("\"events\":[]") != std::string::npos);
}

// Test trace header with client vantage point
TEST(QlogSerializerTest, SerializeTraceHeaderClient) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader(
        "client-conn-123",
        VantagePoint::kClient,
        common_fields,
        config
    );

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

    std::string header = serializer.SerializeTraceHeader(
        "conn-456",
        VantagePoint::kServer,
        common_fields,
        config
    );

    EXPECT_TRUE(header.find("\"group_id\":\"test-group-123\"") != std::string::npos);
}

// Test trace header without group_id
TEST(QlogSerializerTest, SerializeTraceHeaderWithoutGroupId) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_type = "QUIC";
    // group_id left empty

    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader(
        "conn-789",
        VantagePoint::kServer,
        common_fields,
        config
    );

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

    std::string header = serializer.SerializeTraceHeader(
        "test-conn",
        VantagePoint::kServer,
        common_fields,
        config
    );

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
        EXPECT_TRUE(serialized_events[i].find("\"packet_number\":" + std::to_string(i))
                    != std::string::npos);
    }
}

// Test VantagePointToString function (used in serializer)
TEST(QlogSerializerTest, VantagePointStrings) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    // Test client
    std::string header = serializer.SerializeTraceHeader(
        "conn1", VantagePoint::kClient, common_fields, config);
    EXPECT_TRUE(header.find("\"client\"") != std::string::npos);

    // Test server
    header = serializer.SerializeTraceHeader(
        "conn2", VantagePoint::kServer, common_fields, config);
    EXPECT_TRUE(header.find("\"server\"") != std::string::npos);

    // Test network
    header = serializer.SerializeTraceHeader(
        "conn3", VantagePoint::kNetwork, common_fields, config);
    EXPECT_TRUE(header.find("\"network\"") != std::string::npos);

    // Test unknown
    header = serializer.SerializeTraceHeader(
        "conn4", VantagePoint::kUnknown, common_fields, config);
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
    EXPECT_TRUE(json.find("\"packet_number\":" + std::to_string(UINT64_MAX))
                != std::string::npos);
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

}  // namespace
}  // namespace common
}  // namespace quicx
