#include <gtest/gtest.h>

#include "common/buffer/buffer_span.h"
#include "quic/common/version.h"
#include "quic/connection/transport_param.h"
#include "quic/connection/type.h"

namespace quicx {
namespace quic {
namespace {

TEST(transport_param_utest, test1) {
    TransportParam tp1;
    tp1.Init(DEFAULT_QUIC_TRANSPORT_PARAMS);

    uint8_t buf[1024] = {0};
    size_t bytes_written = 0;
    common::BufferSpan write_buffer(buf, sizeof(buf));

    EXPECT_TRUE(tp1.Encode(write_buffer, bytes_written));

    // After encoding, create BufferSpan with the actual written length
    common::BufferSpan read_buffer(buf, static_cast<uint32_t>(bytes_written));

    TransportParam tp2;
    EXPECT_TRUE(tp2.Decode(read_buffer));

    EXPECT_EQ(tp1.GetOriginalDestinationConnectionId(), tp2.GetOriginalDestinationConnectionId());
    EXPECT_EQ(tp1.GetMaxIdleTimeout(), tp2.GetMaxIdleTimeout());
    EXPECT_EQ(tp1.GetStatelessResetToken(), tp2.GetStatelessResetToken());
    EXPECT_EQ(tp1.GetmaxUdpPayloadSize(), tp2.GetmaxUdpPayloadSize());
    EXPECT_EQ(tp1.GetInitialMaxData(), tp2.GetInitialMaxData());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataBidiLocal(), tp2.GetInitialMaxStreamDataBidiLocal());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataBidiRemote(), tp2.GetInitialMaxStreamDataBidiRemote());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataUni(), tp2.GetInitialMaxStreamDataUni());
    EXPECT_EQ(tp1.GetInitialMaxStreamsBidi(), tp2.GetInitialMaxStreamsBidi());
    EXPECT_EQ(tp1.GetInitialMaxStreamsUni(), tp2.GetInitialMaxStreamsUni());
    EXPECT_EQ(tp1.GetackDelayExponent(), tp2.GetackDelayExponent());
    EXPECT_EQ(tp1.GetMaxAckDelay(), tp2.GetMaxAckDelay());
    EXPECT_EQ(tp1.GetDisableActiveMigration(), tp2.GetDisableActiveMigration());
    EXPECT_EQ(tp1.GetPreferredAddress(), tp2.GetPreferredAddress());
    EXPECT_EQ(tp1.GetActiveConnectionIdLimit(), tp2.GetActiveConnectionIdLimit());
    EXPECT_EQ(tp1.GetInitialSourceConnectionId(), tp2.GetInitialSourceConnectionId());
    EXPECT_EQ(tp1.GetRetrySourceConnectionId(), tp2.GetRetrySourceConnectionId());
}

TEST(transport_param_utest, test2) {
    TransportParam tp1;

    uint8_t buf[1024] = {0};
    size_t bytes_written = 0;
    common::BufferSpan write_buffer(buf, sizeof(buf));

    EXPECT_TRUE(tp1.Encode(write_buffer, bytes_written));

    common::BufferSpan read_buffer(buf, static_cast<uint32_t>(bytes_written));
    TransportParam tp2;
    EXPECT_TRUE(tp2.Decode(read_buffer));

    EXPECT_EQ(tp1.GetOriginalDestinationConnectionId(), tp2.GetOriginalDestinationConnectionId());
    EXPECT_EQ(tp1.GetMaxIdleTimeout(), tp2.GetMaxIdleTimeout());
    EXPECT_EQ(tp1.GetStatelessResetToken(), tp2.GetStatelessResetToken());
    EXPECT_EQ(tp1.GetmaxUdpPayloadSize(), tp2.GetmaxUdpPayloadSize());
    EXPECT_EQ(tp1.GetInitialMaxData(), tp2.GetInitialMaxData());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataBidiLocal(), tp2.GetInitialMaxStreamDataBidiLocal());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataBidiRemote(), tp2.GetInitialMaxStreamDataBidiRemote());
    EXPECT_EQ(tp1.GetInitialMaxStreamDataUni(), tp2.GetInitialMaxStreamDataUni());
    EXPECT_EQ(tp1.GetInitialMaxStreamsBidi(), tp2.GetInitialMaxStreamsBidi());
    EXPECT_EQ(tp1.GetInitialMaxStreamsUni(), tp2.GetInitialMaxStreamsUni());
    EXPECT_EQ(tp1.GetackDelayExponent(), tp2.GetackDelayExponent());
    EXPECT_EQ(tp1.GetMaxAckDelay(), tp2.GetMaxAckDelay());
    EXPECT_EQ(tp1.GetDisableActiveMigration(), tp2.GetDisableActiveMigration());
    EXPECT_EQ(tp1.GetPreferredAddress(), tp2.GetPreferredAddress());
    EXPECT_EQ(tp1.GetActiveConnectionIdLimit(), tp2.GetActiveConnectionIdLimit());
    EXPECT_EQ(tp1.GetInitialSourceConnectionId(), tp2.GetInitialSourceConnectionId());
    EXPECT_EQ(tp1.GetRetrySourceConnectionId(), tp2.GetRetrySourceConnectionId());
}

// --------------------------------------------------------------------------
// RFC 9368 §3: version_information transport parameter (id 0x11) round-trip.
// --------------------------------------------------------------------------

// Round-trip encoding of a version_information TP with chosen=v2 and
// available={v2, v1} should preserve all fields exactly.
TEST(transport_param_utest, VersionInformation_RoundTrip) {
    TransportParam tp1;
    tp1.SetVersionInformation(kQuicVersion2, {kQuicVersion2, kQuicVersion1});
    ASSERT_TRUE(tp1.HasVersionInformation());

    uint8_t buf[256] = {0};
    size_t bytes_written = 0;
    common::BufferSpan write_buffer(buf, sizeof(buf));
    ASSERT_TRUE(tp1.Encode(write_buffer, bytes_written));
    EXPECT_GT(bytes_written, 0u);

    common::BufferSpan read_buffer(buf, static_cast<uint32_t>(bytes_written));
    TransportParam tp2;
    ASSERT_TRUE(tp2.Decode(read_buffer));
    EXPECT_TRUE(tp2.HasVersionInformation());
    EXPECT_EQ(tp2.GetChosenVersion(), kQuicVersion2);
    ASSERT_EQ(tp2.GetAvailableVersions().size(), 2u);
    EXPECT_EQ(tp2.GetAvailableVersions()[0], kQuicVersion2);
    EXPECT_EQ(tp2.GetAvailableVersions()[1], kQuicVersion1);
}

// A version_information TP with an empty available_versions list (only the
// chosen version) is valid per RFC 9368 §3 (tp_len == 4, "available versions"
// is zero or more 32-bit fields).
TEST(transport_param_utest, VersionInformation_ChosenOnly) {
    TransportParam tp1;
    tp1.SetVersionInformation(kQuicVersion1, {});

    uint8_t buf[64] = {0};
    size_t bytes_written = 0;
    common::BufferSpan write_buffer(buf, sizeof(buf));
    ASSERT_TRUE(tp1.Encode(write_buffer, bytes_written));

    common::BufferSpan read_buffer(buf, static_cast<uint32_t>(bytes_written));
    TransportParam tp2;
    ASSERT_TRUE(tp2.Decode(read_buffer));
    EXPECT_TRUE(tp2.HasVersionInformation());
    EXPECT_EQ(tp2.GetChosenVersion(), kQuicVersion1);
    EXPECT_TRUE(tp2.GetAvailableVersions().empty());
}

// A version_information TP whose length is not a multiple of 4 must be
// rejected: RFC 9368 §3 requires each field to be exactly 32 bits wide.
TEST(transport_param_utest, VersionInformation_RejectInvalidLength) {
    // Hand-crafted bytes (QUIC varint 1-byte form: 6-bit value in low bits):
    //   varint(0x11) 'version_information' id (1 byte: 0x11)
    //   varint(5)    tp_len; invalid: not a multiple of 4 (1 byte: 0x05)
    //   5 data bytes
    uint8_t buf[] = {
        0x11,                    // TP id
        0x05,                    // tp_len = 5 (INVALID; must be multiple of 4)
        0x00, 0x00, 0x00, 0x01,  // partial "chosen version"
        0xde,                    // extra trailing byte
    };
    common::BufferSpan read_buffer(buf, sizeof(buf));
    TransportParam tp;
    EXPECT_FALSE(tp.Decode(read_buffer));
}

// A version_information TP whose tp_len is 0 must be rejected: RFC 9368 §3
// requires at least a chosen_version field (tp_len >= 4).
TEST(transport_param_utest, VersionInformation_RejectZeroLength) {
    uint8_t buf[] = {
        0x11,  // TP id
        0x00,  // tp_len = 0 (INVALID; must be >= 4)
    };
    common::BufferSpan read_buffer(buf, sizeof(buf));
    TransportParam tp;
    EXPECT_FALSE(tp.Decode(read_buffer));
}

// Merge() must copy peer's version_information so the upper layer can make
// downgrade-detection / upgrade decisions.
TEST(transport_param_utest, VersionInformation_MergedFromPeer) {
    TransportParam local;
    // local has no version_information yet.
    EXPECT_FALSE(local.HasVersionInformation());

    TransportParam peer;
    peer.SetVersionInformation(kQuicVersion2, {kQuicVersion2, kQuicVersion1});
    local.Merge(peer);

    EXPECT_TRUE(local.HasVersionInformation());
    EXPECT_EQ(local.GetChosenVersion(), kQuicVersion2);
    ASSERT_EQ(local.GetAvailableVersions().size(), 2u);
    EXPECT_EQ(local.GetAvailableVersions()[0], kQuicVersion2);
    EXPECT_EQ(local.GetAvailableVersions()[1], kQuicVersion1);
}

}  // namespace
}  // namespace quic
}  // namespace quicx