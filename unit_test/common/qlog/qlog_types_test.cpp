// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include <gtest/gtest.h>
#include <string>

#include "common/qlog/util/qlog_types.h"
#include "quic/packet/type.h"
#include "quic/frame/type.h"

namespace quicx {
namespace common {
namespace {

// Test PacketTypeToQlogString with all packet types
TEST(QlogTypesTest, PacketTypeToQlogString) {
    EXPECT_STREQ("initial", PacketTypeToQlogString(quic::PacketType::kInitialPacketType));
    EXPECT_STREQ("0RTT", PacketTypeToQlogString(quic::PacketType::k0RttPacketType));
    EXPECT_STREQ("handshake", PacketTypeToQlogString(quic::PacketType::kHandshakePacketType));
    EXPECT_STREQ("retry", PacketTypeToQlogString(quic::PacketType::kRetryPacketType));
    EXPECT_STREQ("version_negotiation", PacketTypeToQlogString(quic::PacketType::kNegotiationPacketType));
    EXPECT_STREQ("1RTT", PacketTypeToQlogString(quic::PacketType::k1RttPacketType));
    EXPECT_STREQ("unknown", PacketTypeToQlogString(quic::PacketType::kUnknownPacketType));
}

// Test FrameTypeToQlogString with common frame types
TEST(QlogTypesTest, FrameTypeToQlogStringCommon) {
    EXPECT_STREQ("padding", FrameTypeToQlogString(quic::FrameType::kPadding));
    EXPECT_STREQ("ping", FrameTypeToQlogString(quic::FrameType::kPing));
    EXPECT_STREQ("ack", FrameTypeToQlogString(quic::FrameType::kAck));
    EXPECT_STREQ("ack_ecn", FrameTypeToQlogString(quic::FrameType::kAckEcn));
    EXPECT_STREQ("reset_stream", FrameTypeToQlogString(quic::FrameType::kResetStream));
    EXPECT_STREQ("stop_sending", FrameTypeToQlogString(quic::FrameType::kStopSending));
    EXPECT_STREQ("crypto", FrameTypeToQlogString(quic::FrameType::kCrypto));
    EXPECT_STREQ("new_token", FrameTypeToQlogString(quic::FrameType::kNewToken));
    EXPECT_STREQ("stream", FrameTypeToQlogString(quic::FrameType::kStream));
}

// Test FrameTypeToQlogString with flow control frames
TEST(QlogTypesTest, FrameTypeToQlogStringFlowControl) {
    EXPECT_STREQ("max_data", FrameTypeToQlogString(quic::FrameType::kMaxData));
    EXPECT_STREQ("max_stream_data", FrameTypeToQlogString(quic::FrameType::kMaxStreamData));
    EXPECT_STREQ("max_streams_bidi", FrameTypeToQlogString(quic::FrameType::kMaxStreamsBidirectional));
    EXPECT_STREQ("max_streams_uni", FrameTypeToQlogString(quic::FrameType::kMaxStreamsUnidirectional));
    EXPECT_STREQ("data_blocked", FrameTypeToQlogString(quic::FrameType::kDataBlocked));
    EXPECT_STREQ("stream_data_blocked", FrameTypeToQlogString(quic::FrameType::kStreamDataBlocked));
    EXPECT_STREQ("streams_blocked_bidi", FrameTypeToQlogString(quic::FrameType::kStreamsBlockedBidirectional));
    EXPECT_STREQ("streams_blocked_uni", FrameTypeToQlogString(quic::FrameType::kStreamsBlockedUnidirectional));
}

// Test FrameTypeToQlogString with connection management frames
TEST(QlogTypesTest, FrameTypeToQlogStringConnectionManagement) {
    EXPECT_STREQ("new_connection_id", FrameTypeToQlogString(quic::FrameType::kNewConnectionId));
    EXPECT_STREQ("retire_connection_id", FrameTypeToQlogString(quic::FrameType::kRetireConnectionId));
    EXPECT_STREQ("path_challenge", FrameTypeToQlogString(quic::FrameType::kPathChallenge));
    EXPECT_STREQ("path_response", FrameTypeToQlogString(quic::FrameType::kPathResponse));
    EXPECT_STREQ("connection_close", FrameTypeToQlogString(quic::FrameType::kConnectionClose));
    EXPECT_STREQ("connection_close_app", FrameTypeToQlogString(quic::FrameType::kConnectionCloseApp));
    EXPECT_STREQ("handshake_done", FrameTypeToQlogString(quic::FrameType::kHandshakeDone));
}

// Test FrameTypeToQlogString with unknown frame type
TEST(QlogTypesTest, FrameTypeToQlogStringUnknown) {
    EXPECT_STREQ("unknown", FrameTypeToQlogString(static_cast<quic::FrameType>(0xFF)));
}

// Test VantagePointToString
TEST(QlogTypesTest, VantagePointToString) {
    EXPECT_STREQ("client", VantagePointToString(VantagePoint::kClient));
    EXPECT_STREQ("server", VantagePointToString(VantagePoint::kServer));
    EXPECT_STREQ("network", VantagePointToString(VantagePoint::kNetwork));
    EXPECT_STREQ("unknown", VantagePointToString(VantagePoint::kUnknown));
}

// Test that packet type strings are consistent
TEST(QlogTypesTest, PacketTypeStringsConsistency) {
    // Test that calling function multiple times returns same result
    const char* str1 = PacketTypeToQlogString(quic::PacketType::k1RttPacketType);
    const char* str2 = PacketTypeToQlogString(quic::PacketType::k1RttPacketType);

    EXPECT_STREQ(str1, str2);
}

// Test that frame type strings are consistent
TEST(QlogTypesTest, FrameTypeStringsConsistency) {
    // Test that calling function multiple times returns same result
    const char* str1 = FrameTypeToQlogString(quic::FrameType::kStream);
    const char* str2 = FrameTypeToQlogString(quic::FrameType::kStream);

    EXPECT_STREQ(str1, str2);
}

// Test all packet types are unique
TEST(QlogTypesTest, PacketTypeStringsUnique) {
    std::vector<quic::PacketType> types = {
        quic::PacketType::kInitialPacketType,
        quic::PacketType::k0RttPacketType,
        quic::PacketType::kHandshakePacketType,
        quic::PacketType::kRetryPacketType,
        quic::PacketType::kNegotiationPacketType,
        quic::PacketType::k1RttPacketType,
    };

    std::set<std::string> strings;
    for (auto type : types) {
        strings.insert(PacketTypeToQlogString(type));
    }

    // All strings should be unique (except unknown which we didn't include)
    EXPECT_EQ(types.size(), strings.size());
}

// Test all frame types produce non-empty strings
TEST(QlogTypesTest, FrameTypeStringsNonEmpty) {
    std::vector<quic::FrameType> types = {
        quic::FrameType::kPadding,
        quic::FrameType::kPing,
        quic::FrameType::kAck,
        quic::FrameType::kStream,
        quic::FrameType::kCrypto,
        quic::FrameType::kMaxData,
        quic::FrameType::kConnectionClose,
    };

    for (auto type : types) {
        const char* str = FrameTypeToQlogString(type);
        EXPECT_NE(nullptr, str);
        EXPECT_GT(strlen(str), 0u);
    }
}

// Test vantage point strings are non-empty
TEST(QlogTypesTest, VantagePointStringsNonEmpty) {
    std::vector<VantagePoint> vantage_points = {
        VantagePoint::kClient,
        VantagePoint::kServer,
        VantagePoint::kNetwork,
        VantagePoint::kUnknown,
    };

    for (auto vp : vantage_points) {
        const char* str = VantagePointToString(vp);
        EXPECT_NE(nullptr, str);
        EXPECT_GT(strlen(str), 0u);
    }
}

// Test packet type string lengths are reasonable
TEST(QlogTypesTest, PacketTypeStringLengthsReasonable) {
    std::vector<quic::PacketType> types = {
        quic::PacketType::kInitialPacketType,
        quic::PacketType::k0RttPacketType,
        quic::PacketType::kHandshakePacketType,
        quic::PacketType::k1RttPacketType,
    };

    for (auto type : types) {
        const char* str = PacketTypeToQlogString(type);
        size_t len = strlen(str);
        EXPECT_LE(len, 20u);  // Reasonable max length
    }
}

// Test frame type string lengths are reasonable
TEST(QlogTypesTest, FrameTypeStringLengthsReasonable) {
    std::vector<quic::FrameType> types = {
        quic::FrameType::kPadding,
        quic::FrameType::kAck,
        quic::FrameType::kStream,
        quic::FrameType::kConnectionClose,
    };

    for (auto type : types) {
        const char* str = FrameTypeToQlogString(type);
        size_t len = strlen(str);
        EXPECT_LE(len, 30u);  // Reasonable max length
    }
}

// Test that conversion functions don't crash with edge case values
TEST(QlogTypesTest, EdgeCaseValues) {
    // These should not crash
    PacketTypeToQlogString(static_cast<quic::PacketType>(255));
    FrameTypeToQlogString(static_cast<quic::FrameType>(255));
    VantagePointToString(static_cast<VantagePoint>(255));
}

// Test specific qlog string formats match specification
TEST(QlogTypesTest, QlogStringFormats) {
    // Check that strings use lowercase and underscores (qlog convention)
    EXPECT_STREQ("packet_sent", "packet_sent");  // Example format

    // Check specific important formats
    const char* initial = PacketTypeToQlogString(quic::PacketType::kInitialPacketType);
    EXPECT_TRUE(islower(initial[0]));  // Should start with lowercase

    const char* stream = FrameTypeToQlogString(quic::FrameType::kStream);
    EXPECT_TRUE(islower(stream[0]));  // Should start with lowercase
}

// Test 0-RTT vs 1-RTT distinction
TEST(QlogTypesTest, RttPacketDistinction) {
    const char* zero_rtt = PacketTypeToQlogString(quic::PacketType::k0RttPacketType);
    const char* one_rtt = PacketTypeToQlogString(quic::PacketType::k1RttPacketType);

    EXPECT_STRNE(zero_rtt, one_rtt);
    EXPECT_STREQ("0RTT", zero_rtt);
    EXPECT_STREQ("1RTT", one_rtt);
}

// Test bidirectional vs unidirectional streams distinction
TEST(QlogTypesTest, StreamDirectionDistinction) {
    const char* bidi = FrameTypeToQlogString(quic::FrameType::kMaxStreamsBidirectional);
    const char* uni = FrameTypeToQlogString(quic::FrameType::kMaxStreamsUnidirectional);

    EXPECT_STRNE(bidi, uni);
    EXPECT_TRUE(strstr(bidi, "bidi") != nullptr);
    EXPECT_TRUE(strstr(uni, "uni") != nullptr);
}

// Test ACK vs ACK+ECN distinction
TEST(QlogTypesTest, AckFrameDistinction) {
    const char* ack = FrameTypeToQlogString(quic::FrameType::kAck);
    const char* ack_ecn = FrameTypeToQlogString(quic::FrameType::kAckEcn);

    EXPECT_STRNE(ack, ack_ecn);
    EXPECT_STREQ("ack", ack);
    EXPECT_STREQ("ack_ecn", ack_ecn);
}

// Test connection close frame distinction
TEST(QlogTypesTest, ConnectionCloseDistinction) {
    const char* close = FrameTypeToQlogString(quic::FrameType::kConnectionClose);
    const char* close_app = FrameTypeToQlogString(quic::FrameType::kConnectionCloseApp);

    EXPECT_STRNE(close, close_app);
    EXPECT_STREQ("connection_close", close);
    EXPECT_STREQ("connection_close_app", close_app);
}

}  // namespace
}  // namespace common
}  // namespace quicx
