// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <string>
#include <sstream>

#include "common/qlog/event/connectivity_events.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/serializer/json_seq_serializer.h"
#include "common/qlog/util/qlog_constants.h"

namespace quicx {
namespace common {
namespace {

// All records produced by the serializer are RS-prefixed (\x1E) and
// LF-terminated per RFC 7464 / qlog draft-02 §6.2 (JSON-SEQ).
//
// The helper below strips the leading RS so we can perform substring /
// JSON-balance checks on the raw JSON payload.
static std::string StripRs(const std::string& s) {
    if (!s.empty() && s.front() == kJsonSeqRecordSeparator) {
        return s.substr(1);
    }
    return s;
}

// Lightweight balanced-braces JSON validity check (no external dep).
static bool IsBalancedJson(const std::string& json) {
    int braces = 0;
    int brackets = 0;
    bool in_string = false;
    bool escaped = false;
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

// Test JsonSeqSerializer creation
TEST(QlogSerializerTest, JsonSeqSerializerCreation) {
    JsonSeqSerializer serializer;
    EXPECT_EQ(QlogFileFormat::kSequential, serializer.GetFormat());
}

// Test trace header serialization
TEST(QlogSerializerTest, SerializeTraceHeaderBasic) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_types = {"QUIC"};

    QlogConfiguration config;
    config.time_offset = 0;
    config.time_units = "us";

    std::string header =
        serializer.SerializeTraceHeader("test-connection-id", VantagePoint::kServer, common_fields, config);

    // The record begins with the JSON-SEQ Record Separator and ends with LF.
    EXPECT_EQ(kJsonSeqRecordSeparator, header.front());
    EXPECT_EQ('\n', header.back());

    // Format / version per draft-02
    EXPECT_TRUE(header.find("\"qlog_format\":\"JSON-SEQ\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"qlog_version\":\"draft-02\"") != std::string::npos);

    // Trace metadata is now nested under a single "trace" sub-object.
    EXPECT_TRUE(header.find("\"title\":\"QuicX server\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"trace\":{") != std::string::npos);
    EXPECT_TRUE(header.find("\"vantage_point\":{") != std::string::npos);
    EXPECT_TRUE(header.find("\"name\":\"test-connection-id\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"type\":\"server\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"common_fields\":{") != std::string::npos);
    EXPECT_TRUE(header.find("\"protocol_types\":[\"QUIC\"]") != std::string::npos);
    EXPECT_TRUE(header.find("\"configuration\":{") != std::string::npos);
    EXPECT_TRUE(header.find("\"time_offset\":0") != std::string::npos);
    EXPECT_TRUE(header.find("\"time_units\":\"us\"") != std::string::npos);
}

// Test trace header with client vantage point
TEST(QlogSerializerTest, SerializeTraceHeaderClient) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    std::string header =
        serializer.SerializeTraceHeader("client-conn-123", VantagePoint::kClient, common_fields, config);

    EXPECT_TRUE(header.find("\"title\":\"QuicX client\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"name\":\"client-conn-123\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"type\":\"client\"") != std::string::npos);
}

// Test trace header with group_id
TEST(QlogSerializerTest, SerializeTraceHeaderWithGroupId) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_types = {"QUIC"};
    common_fields.group_id = "test-group-123";

    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader("conn-456", VantagePoint::kServer, common_fields, config);

    EXPECT_TRUE(header.find("\"group_id\":\"test-group-123\"") != std::string::npos);
}

// Test trace header without group_id
TEST(QlogSerializerTest, SerializeTraceHeaderWithoutGroupId) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_types = {"QUIC"};
    // group_id left empty

    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader("conn-789", VantagePoint::kServer, common_fields, config);

    EXPECT_TRUE(header.find("\"group_id\"") == std::string::npos);
}

// Test event serialization with PacketSentData
TEST(QlogSerializerTest, SerializePacketSentEvent) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 123456789;  // 123456 ms (truncated)
    event.name = "quic:packet_sent";

    auto data = std::make_unique<PacketSentData>();
    data->packet_number = 100;
    data->packet_type = quic::PacketType::k1RttPacketType;
    data->packet_size = 1200;
    data->frames.push_back(quic::FrameType::kStream);
    data->frames.push_back(quic::FrameType::kAck);

    event.data = std::move(data);

    std::string json = serializer.SerializeEvent(event);

    // RS prefix and LF suffix
    EXPECT_EQ(kJsonSeqRecordSeparator, json.front());
    EXPECT_EQ('\n', json.back());

    // Per draft-02, time is a string-encoded integer in time_units.
    EXPECT_TRUE(json.find("\"time\":\"123456\"") != std::string::npos);

    // Event name and data
    EXPECT_TRUE(json.find("\"name\":\"quic:packet_sent\"") != std::string::npos);
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

    EXPECT_TRUE(json.find("\"time\":\"0\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"name\":\"quic:connection_started\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"src_ip\":\"192.168.1.100\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"dst_port\":443") != std::string::npos);
}

// Test event serialization without data
TEST(QlogSerializerTest, SerializeEventWithoutData) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;  // 1 ms
    event.name = "test:event";
    event.data = nullptr;

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"time\":\"1\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"name\":\"test:event\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"data\":{}") != std::string::npos);
}

// Test timestamp precision (truncating microsecond fraction to ms integer)
TEST(QlogSerializerTest, TimestampPrecision) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.name = "test:event";
    event.data = std::make_unique<PacketSentData>();

    event.time_us = 0;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":\"0\"") != std::string::npos);

    // 1 us truncates to 0 ms (integer time unit)
    event.time_us = 1;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":\"0\"") != std::string::npos);

    event.time_us = 1000;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":\"1\"") != std::string::npos);

    event.time_us = 1234567;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":\"1234\"") != std::string::npos);

    event.time_us = 999999;
    EXPECT_TRUE(serializer.SerializeEvent(event).find("\"time\":\"999\"") != std::string::npos);
}

// Test JSON-SEQ format compliance: trace header is a single record
// (RS-prefixed, LF-terminated).
TEST(QlogSerializerTest, JsonSeqFormatCompliance) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader("test-conn", VantagePoint::kServer, common_fields, config);

    // Exactly one LF (terminator) and exactly one leading RS.
    EXPECT_EQ(kJsonSeqRecordSeparator, header.front());
    EXPECT_EQ('\n', header.back());
    size_t newline_count = std::count(header.begin(), header.end(), '\n');
    EXPECT_EQ(1u, newline_count);
    size_t rs_count = std::count(header.begin(), header.end(), kJsonSeqRecordSeparator);
    EXPECT_EQ(1u, rs_count);
}

// Test event JSON line format
TEST(QlogSerializerTest, EventJsonLineFormat) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;
    event.name = "test:event";
    event.data = std::make_unique<PacketSentData>();

    std::string json = serializer.SerializeEvent(event);

    // RS prefix + JSON object + LF terminator
    EXPECT_EQ(kJsonSeqRecordSeparator, json.front());
    EXPECT_EQ('\n', json.back());

    // Strip RS and LF, the remaining payload should be a balanced JSON object.
    std::string payload = StripRs(json);
    payload.pop_back();  // remove '\n'
    EXPECT_EQ('{', payload.front());
    EXPECT_EQ('}', payload.back());
    EXPECT_TRUE(IsBalancedJson(payload));
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

    for (size_t i = 0; i < serialized_events.size(); i++) {
        EXPECT_TRUE(serialized_events[i].find("\"packet_number\":" + std::to_string(i)) != std::string::npos);
    }
}

// Test VantagePointToString function (used in serializer)
TEST(QlogSerializerTest, VantagePointStrings) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader("conn1", VantagePoint::kClient, common_fields, config);
    EXPECT_TRUE(header.find("\"client\"") != std::string::npos);

    header = serializer.SerializeTraceHeader("conn2", VantagePoint::kServer, common_fields, config);
    EXPECT_TRUE(header.find("\"server\"") != std::string::npos);

    header = serializer.SerializeTraceHeader("conn3", VantagePoint::kNetwork, common_fields, config);
    EXPECT_TRUE(header.find("\"network\"") != std::string::npos);

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

    event.data = std::move(data);

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"frames\":[]") != std::string::npos);
}

// ====================================================================
// Extended: JSON validity verification for each serialized record
// ====================================================================

// Test: Trace header is independently parseable JSON (single record).
TEST(QlogSerializerTest, HeaderIsIndependentJson) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_types = {"QUIC"};
    common_fields.group_id = "test-group";

    QlogConfiguration config;
    config.time_offset = 0;
    config.time_units = "ms";

    std::string header = serializer.SerializeTraceHeader(
        "conn-json-test", VantagePoint::kServer, common_fields, config);

    // Strip RS prefix and trailing LF; the payload must be balanced JSON.
    std::string payload = StripRs(header);
    ASSERT_FALSE(payload.empty());
    EXPECT_EQ('\n', payload.back());
    payload.pop_back();

    EXPECT_EQ('{', payload.front()) << "Header payload should start with '{': " << payload;
    EXPECT_EQ('}', payload.back()) << "Header payload should end with '}': " << payload;
    EXPECT_TRUE(IsBalancedJson(payload)) << "Header is not balanced JSON: " << payload;
}

// Test: Event line is independently parseable JSON
TEST(QlogSerializerTest, EventLineIsIndependentJson) {
    JsonSeqSerializer serializer;

    auto check_event = [&](std::unique_ptr<EventData> data, const std::string& name) {
        QlogEvent event;
        event.time_us = 100000;
        event.name = name;
        event.data = std::move(data);

        std::string json = serializer.SerializeEvent(event);
        std::string payload = StripRs(json);
        payload.pop_back();  // strip '\n'

        EXPECT_EQ('{', payload.front()) << name << ": " << payload;
        EXPECT_EQ('}', payload.back()) << name << ": " << payload;
        EXPECT_TRUE(IsBalancedJson(payload)) << name << " is not balanced JSON: " << payload;
    };

    {
        auto data = std::make_unique<PacketSentData>();
        data->packet_number = 42;
        data->packet_type = quic::PacketType::k1RttPacketType;
        data->packet_size = 1200;
        data->frames.push_back(quic::FrameType::kStream);
        check_event(std::move(data), "quic:packet_sent");
    }
    {
        auto data = std::make_unique<ConnectionStartedData>();
        data->src_ip = "192.168.1.1";
        data->src_port = 443;
        data->dst_ip = "10.0.0.1";
        data->dst_port = 8080;
        check_event(std::move(data), "quic:connection_started");
    }
    {
        auto data = std::make_unique<RecoveryMetricsData>();
        data->min_rtt_us = 5000;
        data->cwnd_bytes = 14520;
        data->ssthresh = 20000;
        data->pacing_rate_bps = 1000000;
        check_event(std::move(data), "recovery:metrics_updated");
    }
}

// Test: Header contains required qlog_format and qlog_version fields.
TEST(QlogSerializerTest, HeaderRequiredTopLevelFields) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    QlogConfiguration config;

    std::string header = serializer.SerializeTraceHeader(
        "conn-hdr", VantagePoint::kClient, common_fields, config);

    EXPECT_TRUE(header.find("\"qlog_format\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"qlog_version\"") != std::string::npos);
}

// Test: Header contains required vantage_point, common_fields, configuration
// (all nested under the "trace" sub-object per draft-02).
TEST(QlogSerializerTest, HeaderTraceSubObjectRequiredFields) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_types = {"QUIC"};

    QlogConfiguration config;
    config.time_offset = 0;
    config.time_units = "ms";

    std::string header = serializer.SerializeTraceHeader(
        "conn-meta", VantagePoint::kServer, common_fields, config);

    EXPECT_TRUE(header.find("\"trace\":{") != std::string::npos);
    EXPECT_TRUE(header.find("\"vantage_point\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"common_fields\"") != std::string::npos);
    EXPECT_TRUE(header.find("\"configuration\"") != std::string::npos);
}

// Test: Event contains required fields: time, name, data
TEST(QlogSerializerTest, EventContainsRequiredFields) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 5000;
    event.name = "quic:test_event";
    event.data = std::make_unique<PacketSentData>();

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"time\":") != std::string::npos);
    EXPECT_TRUE(json.find("\"name\":") != std::string::npos);
    EXPECT_TRUE(json.find("\"data\":") != std::string::npos);
}

// Test: Timestamp monotonicity across sequential events
TEST(QlogSerializerTest, TimestampMonotonicity) {
    JsonSeqSerializer serializer;

    std::vector<uint64_t> timestamps;

    for (uint64_t i = 0; i < 10; i++) {
        QlogEvent event;
        event.time_us = i * 1000;  // 0, 1000, 2000, ... us → 0,1,2,... ms
        event.name = "test:event";
        event.data = std::make_unique<PacketSentData>();

        std::string json = serializer.SerializeEvent(event);

        // Time is a string in JSON: `"time":"<int>"`. Find and parse it.
        const std::string key = "\"time\":\"";
        size_t key_pos = json.find(key);
        ASSERT_NE(std::string::npos, key_pos);
        size_t value_start = key_pos + key.size();
        size_t value_end = json.find('"', value_start);
        ASSERT_NE(std::string::npos, value_end);
        std::string time_str = json.substr(value_start, value_end - value_start);
        timestamps.push_back(std::stoull(time_str));
    }

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

    EXPECT_TRUE(json.find("\"group_id\":\"connection-group-42\"") != std::string::npos);
}

// Test: Event without group_id does NOT include group_id field
TEST(QlogSerializerTest, EventWithoutGroupId) {
    JsonSeqSerializer serializer;

    QlogEvent event;
    event.time_us = 1000;
    event.name = "quic:test";
    event.data = std::make_unique<PacketSentData>();

    std::string json = serializer.SerializeEvent(event);

    EXPECT_TRUE(json.find("\"group_id\"") == std::string::npos);
}

// Test: Full qlog output (header + events) forms valid JSON-SEQ stream
TEST(QlogSerializerTest, FullQlogOutputIsValidJsonSeq) {
    JsonSeqSerializer serializer;

    CommonFields common_fields;
    common_fields.protocol_types = {"QUIC"};
    QlogConfiguration config;
    config.time_offset = 0;
    config.time_units = "ms";

    std::string output = serializer.SerializeTraceHeader(
        "conn-full", VantagePoint::kClient, common_fields, config);

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

    // Split on LF; each non-empty line should start with RS and the JSON
    // payload that follows must be balanced.
    int record_count = 0;
    size_t pos = 0;
    while (pos < output.size()) {
        size_t lf = output.find('\n', pos);
        if (lf == std::string::npos) break;
        std::string record = output.substr(pos, lf - pos);
        pos = lf + 1;
        if (record.empty()) continue;
        record_count++;
        EXPECT_EQ(kJsonSeqRecordSeparator, record.front())
            << "Record " << record_count << " should start with RS";
        std::string payload = record.substr(1);
        EXPECT_TRUE(IsBalancedJson(payload))
            << "Record " << record_count << " not balanced JSON: " << payload;
    }

    // 1 trace header record + 5 event records = 6 total
    EXPECT_EQ(6, record_count);
}

}  // namespace
}  // namespace common
}  // namespace quicx
