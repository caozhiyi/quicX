// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include "common/qlog/event/qlog_event.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/connectivity_events.h"

namespace quicx {
namespace common {
namespace {

// Test EventData base class and JSON escaping
TEST(QlogEventTest, JsonEscaping) {
    class TestEventData : public EventData {
    public:
        std::string test_string;
        std::string ToJson() const override {
            return "{\"test\":\"" + EscapeJson(test_string) + "\"}";
        }
    };

    // Test special characters
    TestEventData data;
    data.test_string = "Hello \"World\"";
    EXPECT_EQ("{\"test\":\"Hello \\\"World\\\"\"}", data.ToJson());

    data.test_string = "Line1\nLine2";
    EXPECT_EQ("{\"test\":\"Line1\\nLine2\"}", data.ToJson());

    data.test_string = "Tab\there";
    EXPECT_EQ("{\"test\":\"Tab\\there\"}", data.ToJson());

    data.test_string = "Backslash\\here";
    EXPECT_EQ("{\"test\":\"Backslash\\\\here\"}", data.ToJson());

    // Test control characters
    data.test_string = std::string(1, '\x01') + "test";
    std::string result = data.ToJson();
    EXPECT_TRUE(result.find("\\u0001") != std::string::npos);
}

// Test PacketSentData
TEST(QlogEventTest, PacketSentDataSerialization) {
    PacketSentData data;
    data.packet_number = 12345;
    data.packet_type = quic::PacketType::k1RttPacketType;
    data.packet_size = 1200;
    data.frames.push_back(quic::FrameType::kStream);
    data.frames.push_back(quic::FrameType::kAck);

    std::string json = data.ToJson();

    // Verify JSON structure
    EXPECT_TRUE(json.find("\"packet_type\":\"1RTT\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"packet_number\":12345") != std::string::npos);
    EXPECT_TRUE(json.find("\"packet_size\":1200") != std::string::npos);
    EXPECT_TRUE(json.find("\"frames\":[") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"stream\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"ack\"") != std::string::npos);
}

// Test PacketSentData with raw payload
TEST(QlogEventTest, PacketSentDataWithRawPayload) {
    PacketSentData data;
    data.packet_number = 100;
    data.packet_type = quic::PacketType::kInitialPacketType;
    data.packet_size = 1200;
    data.raw.enabled = true;
    data.raw.payload_hex = "0123456789abcdef";

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"packet_type\":\"initial\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"raw\":{\"payload\":\"0123456789abcdef\"}") != std::string::npos);
}

// Test PacketReceivedData
TEST(QlogEventTest, PacketReceivedDataSerialization) {
    PacketReceivedData data;
    data.packet_number = 54321;
    data.packet_type = quic::PacketType::kHandshakePacketType;
    data.packet_size = 800;
    data.frames.push_back(quic::FrameType::kCrypto);
    data.frames.push_back(quic::FrameType::kAck);

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"packet_type\":\"handshake\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"packet_number\":54321") != std::string::npos);
    EXPECT_TRUE(json.find("\"packet_size\":800") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"crypto\"") != std::string::npos);
}

// Test PacketsAckedData
TEST(QlogEventTest, PacketsAckedDataSerialization) {
    PacketsAckedData data;
    data.ack_ranges.push_back({100, 105});
    data.ack_ranges.push_back({110, 120});
    data.ack_delay_us = 25000;

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"acked_ranges\":[[100,105],[110,120]]") != std::string::npos);
    EXPECT_TRUE(json.find("\"ack_delay\":25000") != std::string::npos);
}

// Test PacketLostData
TEST(QlogEventTest, PacketLostDataSerialization) {
    PacketLostData data;
    data.packet_number = 999;
    data.packet_type = quic::PacketType::k1RttPacketType;
    data.trigger = "time_threshold";

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"packet_number\":999") != std::string::npos);
    EXPECT_TRUE(json.find("\"packet_type\":\"1RTT\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"trigger\":\"time_threshold\"") != std::string::npos);
}

// Test RecoveryMetricsData
TEST(QlogEventTest, RecoveryMetricsDataSerialization) {
    RecoveryMetricsData data;
    data.min_rtt_us = 10000;
    data.smoothed_rtt_us = 12000;
    data.latest_rtt_us = 11000;
    data.rtt_variance_us = 1000;
    data.cwnd_bytes = 14520;
    data.bytes_in_flight = 5000;
    data.ssthresh = 20000;
    data.pacing_rate_bps = 1000000;

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"min_rtt\":10000") != std::string::npos);
    EXPECT_TRUE(json.find("\"smoothed_rtt\":12000") != std::string::npos);
    EXPECT_TRUE(json.find("\"latest_rtt\":11000") != std::string::npos);
    EXPECT_TRUE(json.find("\"cwnd\":14520") != std::string::npos);
    EXPECT_TRUE(json.find("\"bytes_in_flight\":5000") != std::string::npos);
    EXPECT_TRUE(json.find("\"ssthresh\":20000") != std::string::npos);
    EXPECT_TRUE(json.find("\"pacing_rate\":1000000") != std::string::npos);
}

// Test RecoveryMetricsData with default ssthresh
TEST(QlogEventTest, RecoveryMetricsDataDefaultSsthresh) {
    RecoveryMetricsData data;
    data.min_rtt_us = 10000;
    data.cwnd_bytes = 14520;
    // ssthresh left at default UINT64_MAX

    std::string json = data.ToJson();

    // Should not include ssthresh when it's UINT64_MAX
    EXPECT_TRUE(json.find("\"ssthresh\"") == std::string::npos);
}

// Test CongestionStateUpdatedData
TEST(QlogEventTest, CongestionStateUpdatedDataSerialization) {
    CongestionStateUpdatedData data;
    data.old_state = "slow_start";
    data.new_state = "congestion_avoidance";

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"old\":\"slow_start\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"new\":\"congestion_avoidance\"") != std::string::npos);
}

// Test ConnectionStartedData
TEST(QlogEventTest, ConnectionStartedDataSerialization) {
    ConnectionStartedData data;
    data.src_ip = "192.168.1.100";
    data.src_port = 50123;
    data.dst_ip = "10.0.0.1";
    data.dst_port = 443;
    data.src_cid = "a1b2c3d4";
    data.dst_cid = "e5f6a7b8";
    data.protocol = "QUIC";
    data.ip_version = "ipv4";

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"src_ip\":\"192.168.1.100\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"src_port\":50123") != std::string::npos);
    EXPECT_TRUE(json.find("\"dst_ip\":\"10.0.0.1\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"dst_port\":443") != std::string::npos);
    EXPECT_TRUE(json.find("\"src_cid\":\"a1b2c3d4\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"dst_cid\":\"e5f6a7b8\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"protocol\":\"QUIC\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"ip_version\":\"ipv4\"") != std::string::npos);
}

// Test ConnectionClosedData
TEST(QlogEventTest, ConnectionClosedDataSerialization) {
    ConnectionClosedData data;
    data.error_code = 0x01;
    data.reason = "Internal error";
    data.trigger = "error";

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"error_code\":1") != std::string::npos);
    EXPECT_TRUE(json.find("\"reason\":\"Internal error\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"trigger\":\"error\"") != std::string::npos);
}

// Test ConnectionClosedData with special characters in reason
TEST(QlogEventTest, ConnectionClosedDataWithSpecialChars) {
    ConnectionClosedData data;
    data.error_code = 0x0A;
    data.reason = "Connection \"timeout\" occurred\nat network layer";
    data.trigger = "error";

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\\\"timeout\\\"") != std::string::npos);
    EXPECT_TRUE(json.find("\\n") != std::string::npos);
}

// Test StreamStateUpdatedData
TEST(QlogEventTest, StreamStateUpdatedDataSerialization) {
    StreamStateUpdatedData data;
    data.stream_id = 4;
    data.old_state = "ready";
    data.new_state = "open";

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"stream_id\":4") != std::string::npos);
    EXPECT_TRUE(json.find("\"old\":\"ready\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"new\":\"open\"") != std::string::npos);
}

// Test ConnectionStateUpdatedData
TEST(QlogEventTest, ConnectionStateUpdatedDataSerialization) {
    ConnectionStateUpdatedData data;
    data.old_state = "handshake";
    data.new_state = "connected";

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"old\":\"handshake\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"new\":\"connected\"") != std::string::npos);
}

// Test QlogEvent structure
TEST(QlogEventTest, QlogEventMoveSemantics) {
    QlogEvent event1;
    event1.time_us = 123456;
    event1.name = "test_event";
    event1.data = std::make_unique<PacketSentData>();
    event1.group_id = "group1";
    event1.protocol_type = 0;

    // Test move constructor
    QlogEvent event2(std::move(event1));
    EXPECT_EQ(123456u, event2.time_us);
    EXPECT_EQ("test_event", event2.name);
    EXPECT_NE(nullptr, event2.data);
    EXPECT_EQ("group1", event2.group_id);
    EXPECT_EQ(0, event2.protocol_type);

    // Test move assignment
    QlogEvent event3;
    event3 = std::move(event2);
    EXPECT_EQ(123456u, event3.time_us);
    EXPECT_EQ("test_event", event3.name);
    EXPECT_NE(nullptr, event3.data);
}

// Test multiple packet types
TEST(QlogEventTest, AllPacketTypes) {
    struct TestCase {
        quic::PacketType type;
        std::string expected_string;
    };

    std::vector<TestCase> test_cases = {
        {quic::PacketType::kInitialPacketType, "initial"},
        {quic::PacketType::k0RttPacketType, "0RTT"},
        {quic::PacketType::kHandshakePacketType, "handshake"},
        {quic::PacketType::kRetryPacketType, "retry"},
        {quic::PacketType::kNegotiationPacketType, "version_negotiation"},
        {quic::PacketType::k1RttPacketType, "1RTT"},
        {quic::PacketType::kUnknownPacketType, "unknown"},
    };

    for (const auto& test_case : test_cases) {
        PacketSentData data;
        data.packet_number = 1;
        data.packet_type = test_case.type;
        data.packet_size = 100;

        std::string json = data.ToJson();
        EXPECT_TRUE(json.find("\"packet_type\":\"" + test_case.expected_string + "\"")
                    != std::string::npos)
            << "Failed for packet type: " << test_case.expected_string;
    }
}

// Test multiple frame types
TEST(QlogEventTest, AllFrameTypes) {
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

    std::string json = data.ToJson();

    EXPECT_TRUE(json.find("\"frame_type\":\"padding\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"ping\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"ack\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"stream\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"crypto\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"max_data\"") != std::string::npos);
    EXPECT_TRUE(json.find("\"frame_type\":\"connection_close\"") != std::string::npos);
}

// ====================================================================
// Extended: Complete field coverage for all event types
// ====================================================================

// Helper to verify a JSON string contains a key
static bool JsonHasKey(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\"") != std::string::npos;
}

// Test: PacketSentData full field coverage
TEST(QlogEventTest, PacketSentDataFullFields) {
    PacketSentData data;
    data.packet_number = 42;
    data.packet_type = quic::PacketType::kInitialPacketType;
    data.packet_size = 1350;
    data.frames.push_back(quic::FrameType::kCrypto);
    data.frames.push_back(quic::FrameType::kPadding);
    data.raw.enabled = true;
    data.raw.payload_hex = "deadbeef";

    std::string json = data.ToJson();

    // Required fields
    EXPECT_TRUE(JsonHasKey(json, "packet_type")) << json;
    EXPECT_TRUE(JsonHasKey(json, "packet_number")) << json;
    EXPECT_TRUE(JsonHasKey(json, "packet_size")) << json;
    EXPECT_TRUE(JsonHasKey(json, "frames")) << json;

    // Optional raw field
    EXPECT_TRUE(JsonHasKey(json, "raw")) << json;
    EXPECT_TRUE(json.find("deadbeef") != std::string::npos) << json;

    // Nested header structure
    EXPECT_TRUE(JsonHasKey(json, "header")) << json;
}

// Test: PacketReceivedData full field coverage
TEST(QlogEventTest, PacketReceivedDataFullFields) {
    PacketReceivedData data;
    data.packet_number = 99;
    data.packet_type = quic::PacketType::kHandshakePacketType;
    data.packet_size = 600;
    data.frames.push_back(quic::FrameType::kAck);
    data.frames.push_back(quic::FrameType::kCrypto);

    std::string json = data.ToJson();

    EXPECT_TRUE(JsonHasKey(json, "packet_type")) << json;
    EXPECT_TRUE(JsonHasKey(json, "packet_number")) << json;
    EXPECT_TRUE(JsonHasKey(json, "packet_size")) << json;
    EXPECT_TRUE(JsonHasKey(json, "frames")) << json;
    EXPECT_TRUE(JsonHasKey(json, "header")) << json;
}

// Test: PacketsAckedData field coverage
TEST(QlogEventTest, PacketsAckedDataFullFields) {
    PacketsAckedData data;
    data.ack_ranges.push_back({0, 10});
    data.ack_ranges.push_back({20, 30});
    data.ack_ranges.push_back({50, 100});
    data.ack_delay_us = 15000;

    std::string json = data.ToJson();

    EXPECT_TRUE(JsonHasKey(json, "acked_ranges")) << json;
    EXPECT_TRUE(JsonHasKey(json, "ack_delay")) << json;

    // Verify multiple ranges
    EXPECT_TRUE(json.find("[0,10]") != std::string::npos) << json;
    EXPECT_TRUE(json.find("[20,30]") != std::string::npos) << json;
    EXPECT_TRUE(json.find("[50,100]") != std::string::npos) << json;
}

// Test: PacketLostData field coverage with all trigger types
TEST(QlogEventTest, PacketLostDataAllTriggers) {
    std::vector<std::string> triggers = {
        "time_threshold", "packet_threshold", "pto_expired"
    };

    for (const auto& trigger : triggers) {
        PacketLostData data;
        data.packet_number = 42;
        data.packet_type = quic::PacketType::k1RttPacketType;
        data.trigger = trigger;

        std::string json = data.ToJson();

        EXPECT_TRUE(JsonHasKey(json, "packet_number")) << "trigger=" << trigger << " json=" << json;
        EXPECT_TRUE(JsonHasKey(json, "packet_type")) << "trigger=" << trigger << " json=" << json;
        EXPECT_TRUE(JsonHasKey(json, "trigger")) << "trigger=" << trigger << " json=" << json;
        EXPECT_TRUE(json.find("\"trigger\":\"" + trigger + "\"") != std::string::npos)
            << "trigger=" << trigger << " json=" << json;
    }
}

// Test: RecoveryMetricsData full fields (all optional fields present)
TEST(QlogEventTest, RecoveryMetricsDataFullFields) {
    RecoveryMetricsData data;
    data.min_rtt_us = 5000;
    data.smoothed_rtt_us = 6000;
    data.latest_rtt_us = 5500;
    data.rtt_variance_us = 500;
    data.cwnd_bytes = 65535;
    data.bytes_in_flight = 32000;
    data.ssthresh = 40000;
    data.pacing_rate_bps = 2000000;

    std::string json = data.ToJson();

    // All RTT fields
    EXPECT_TRUE(JsonHasKey(json, "min_rtt")) << json;
    EXPECT_TRUE(JsonHasKey(json, "smoothed_rtt")) << json;
    EXPECT_TRUE(JsonHasKey(json, "latest_rtt")) << json;
    EXPECT_TRUE(JsonHasKey(json, "rtt_variance")) << json;

    // Congestion fields
    EXPECT_TRUE(JsonHasKey(json, "cwnd")) << json;
    EXPECT_TRUE(JsonHasKey(json, "bytes_in_flight")) << json;

    // Optional fields (present when non-default)
    EXPECT_TRUE(JsonHasKey(json, "ssthresh")) << json;
    EXPECT_TRUE(JsonHasKey(json, "pacing_rate")) << json;
}

// Test: RecoveryMetricsData minimal fields (optional fields absent)
TEST(QlogEventTest, RecoveryMetricsDataMinimalFields) {
    RecoveryMetricsData data;
    data.min_rtt_us = 5000;
    data.smoothed_rtt_us = 6000;
    data.latest_rtt_us = 5500;
    data.rtt_variance_us = 500;
    data.cwnd_bytes = 14520;
    data.bytes_in_flight = 0;
    // ssthresh defaults to UINT64_MAX (should be omitted)
    // pacing_rate defaults to 0 (should be omitted)

    std::string json = data.ToJson();

    EXPECT_TRUE(JsonHasKey(json, "min_rtt")) << json;
    EXPECT_TRUE(JsonHasKey(json, "cwnd")) << json;

    // Optional fields should be absent
    EXPECT_FALSE(JsonHasKey(json, "ssthresh"))
        << "ssthresh should be omitted when UINT64_MAX: " << json;
    EXPECT_FALSE(JsonHasKey(json, "pacing_rate"))
        << "pacing_rate should be omitted when 0: " << json;
}

// Test: CongestionStateUpdatedData field coverage
TEST(QlogEventTest, CongestionStateUpdatedDataFullFields) {
    std::vector<std::pair<std::string, std::string>> transitions = {
        {"slow_start", "congestion_avoidance"},
        {"congestion_avoidance", "recovery"},
        {"recovery", "slow_start"},
        {"slow_start", "application_limited"},
    };

    for (const auto& [from, to] : transitions) {
        CongestionStateUpdatedData data;
        data.old_state = from;
        data.new_state = to;

        std::string json = data.ToJson();

        EXPECT_TRUE(JsonHasKey(json, "old")) << "from=" << from << " json=" << json;
        EXPECT_TRUE(JsonHasKey(json, "new")) << "to=" << to << " json=" << json;
        EXPECT_TRUE(json.find("\"old\":\"" + from + "\"") != std::string::npos) << json;
        EXPECT_TRUE(json.find("\"new\":\"" + to + "\"") != std::string::npos) << json;
    }
}

// Test: ConnectionStartedData full field coverage
TEST(QlogEventTest, ConnectionStartedDataFullFields) {
    ConnectionStartedData data;
    data.src_ip = "2001:db8::1";
    data.src_port = 12345;
    data.dst_ip = "2001:db8::2";
    data.dst_port = 443;
    data.src_cid = "a1b2c3d4e5f6";
    data.dst_cid = "f6e5d4c3b2a1";
    data.protocol = "QUIC";
    data.ip_version = "ipv6";

    std::string json = data.ToJson();

    EXPECT_TRUE(JsonHasKey(json, "ip_version")) << json;
    EXPECT_TRUE(JsonHasKey(json, "src_ip")) << json;
    EXPECT_TRUE(JsonHasKey(json, "dst_ip")) << json;
    EXPECT_TRUE(JsonHasKey(json, "protocol")) << json;
    EXPECT_TRUE(JsonHasKey(json, "src_port")) << json;
    EXPECT_TRUE(JsonHasKey(json, "dst_port")) << json;
    EXPECT_TRUE(JsonHasKey(json, "src_cid")) << json;
    EXPECT_TRUE(JsonHasKey(json, "dst_cid")) << json;

    // Verify IPv6 address is preserved
    EXPECT_TRUE(json.find("2001:db8::1") != std::string::npos) << json;
}

// Test: ConnectionClosedData full field coverage with all triggers
TEST(QlogEventTest, ConnectionClosedDataAllTriggers) {
    std::vector<std::string> triggers = {
        "clean", "application", "error", "stateless_reset"
    };

    for (const auto& trigger : triggers) {
        ConnectionClosedData data;
        data.error_code = 0x42;
        data.reason = "Test reason for " + trigger;
        data.trigger = trigger;

        std::string json = data.ToJson();

        EXPECT_TRUE(JsonHasKey(json, "error_code")) << "trigger=" << trigger << " json=" << json;
        EXPECT_TRUE(JsonHasKey(json, "reason")) << "trigger=" << trigger << " json=" << json;
        EXPECT_TRUE(JsonHasKey(json, "trigger")) << "trigger=" << trigger << " json=" << json;
    }
}

// Test: ConnectionStateUpdatedData field coverage with all state transitions
TEST(QlogEventTest, ConnectionStateUpdatedDataAllStates) {
    std::vector<std::string> states = {
        "initial", "handshake", "connected", "closing", "draining", "closed"
    };

    for (size_t i = 0; i + 1 < states.size(); i++) {
        ConnectionStateUpdatedData data;
        data.old_state = states[i];
        data.new_state = states[i + 1];

        std::string json = data.ToJson();

        EXPECT_TRUE(JsonHasKey(json, "old")) << json;
        EXPECT_TRUE(JsonHasKey(json, "new")) << json;
    }
}

// Test: StreamStateUpdatedData field coverage
TEST(QlogEventTest, StreamStateUpdatedDataFullFields) {
    StreamStateUpdatedData data;
    data.stream_id = 16;  // Bidirectional stream
    data.old_state = "idle";
    data.new_state = "open";

    std::string json = data.ToJson();

    EXPECT_TRUE(JsonHasKey(json, "stream_id")) << json;
    EXPECT_TRUE(JsonHasKey(json, "old")) << json;
    EXPECT_TRUE(JsonHasKey(json, "new")) << json;
    EXPECT_TRUE(json.find("\"stream_id\":16") != std::string::npos) << json;
}

// Test: All event data classes produce valid JSON (balanced braces)
TEST(QlogEventTest, AllEventDataProduceBalancedJson) {
    auto IsBalanced = [](const std::string& json) -> bool {
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
    };

    // PacketSentData
    {
        PacketSentData data;
        data.packet_number = 1;
        data.packet_type = quic::PacketType::k1RttPacketType;
        data.packet_size = 1200;
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "PacketSentData: " << data.ToJson();
    }

    // PacketReceivedData
    {
        PacketReceivedData data;
        data.packet_number = 1;
        data.packet_type = quic::PacketType::k1RttPacketType;
        data.packet_size = 800;
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "PacketReceivedData: " << data.ToJson();
    }

    // PacketsAckedData
    {
        PacketsAckedData data;
        data.ack_ranges.push_back({1, 10});
        data.ack_delay_us = 5000;
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "PacketsAckedData: " << data.ToJson();
    }

    // PacketLostData
    {
        PacketLostData data;
        data.packet_number = 5;
        data.packet_type = quic::PacketType::k1RttPacketType;
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "PacketLostData: " << data.ToJson();
    }

    // StreamStateUpdatedData
    {
        StreamStateUpdatedData data;
        data.stream_id = 4;
        data.old_state = "idle";
        data.new_state = "open";
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "StreamStateUpdatedData: " << data.ToJson();
    }

    // RecoveryMetricsData
    {
        RecoveryMetricsData data;
        data.min_rtt_us = 5000;
        data.cwnd_bytes = 14520;
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "RecoveryMetricsData: " << data.ToJson();
    }

    // CongestionStateUpdatedData
    {
        CongestionStateUpdatedData data;
        data.old_state = "slow_start";
        data.new_state = "recovery";
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "CongestionStateUpdatedData: " << data.ToJson();
    }

    // ConnectionStartedData
    {
        ConnectionStartedData data;
        data.src_ip = "127.0.0.1";
        data.dst_ip = "127.0.0.1";
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "ConnectionStartedData: " << data.ToJson();
    }

    // ConnectionClosedData
    {
        ConnectionClosedData data;
        data.error_code = 0;
        data.reason = "Normal";
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "ConnectionClosedData: " << data.ToJson();
    }

    // ConnectionStateUpdatedData
    {
        ConnectionStateUpdatedData data;
        data.old_state = "handshake";
        data.new_state = "connected";
        EXPECT_TRUE(IsBalanced(data.ToJson())) << "ConnectionStateUpdatedData: " << data.ToJson();
    }
}

}  // namespace
}  // namespace common
}  // namespace quicx
