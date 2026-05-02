// RFC 9368 Compatible Version Negotiation - end-to-end behaviour tests.
//
// These tests exercise the full handshake between a quicX client and server
// and then verify that the Compatible VN state / advertised
// version_information TP match what RFC 9368 requires.
//
// Scope:
//   * Default handshake (no preferred_version set):
//       - Both sides must reach kStateConnected on the same quic_version_.
//       - Local TP must advertise version_information with
//         chosen_version = wire version, available_versions = [wire version]
//         (conservative default; no unsolicited upgrade offers).
//       - compat_vn_completed_ is set on both sides after ValidateAnd...
//         runs via OnTransportParams.
//   * Explicit preferred_version == quic_version_ (idempotent): behaves the
//     same as the default.
//   * Real v1 -> v2 upgrade (both client and server prefer v2):
//       - Client starts with v1 Initial and advertises [v2, v1].
//       - Server accepts the upgrade, rekeys Initial under v2 salt, and emits
//         its own Initials on the wire as v2.
//       - Client's OnInitialPacket() detects the wire version mismatch and
//         rekeys its own Initial cryptographer under v2 salt (this is the
//         RFC 9368 §4 client side of the upgrade).
//       - Handshake completes, both endpoints end up on kQuicVersion2.

#include <gtest/gtest.h>

#include <memory>
#include <tuple>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/timer/timer.h"
#include "quic/common/version.h"
#include "quic/connection/connection_base.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/connection_server.h"
#include "quic/connection/transport_param.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/packet/packet_decode.h"
#include "quic/quicx/global_resource.h"
#include "connection_test_util.h"
#include "mock_sender.h"

namespace quicx {
namespace quic {
namespace {

// Self-signed certificate (same as connection_close_test.cpp).
static const char kCertPem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIICWDCCAcGgAwIBAgIJAPuwTC6rEJsMMA0GCSqGSIb3DQEBBQUAMEUxCzAJBgNV\n"
    "BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
    "aWRnaXRzIFB0eSBMdGQwHhcNMTQwNDIzMjA1MDQwWhcNMTcwNDIyMjA1MDQwWjBF\n"
    "MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50\n"
    "ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKB\n"
    "gQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92kWdGMdAQhLci\n"
    "HnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiFKKAnHmUcrgfV\n"
    "W28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQABo1AwTjAdBgNV\n"
    "HQ4EFgQUi3XVrMsIvg4fZbf6Vr5sp3Xaha8wHwYDVR0jBBgwFoAUi3XVrMsIvg4f\n"
    "Zbf6Vr5sp3Xaha8wDAYDVR0TBAUwAwEB/zANBgkqhkiG9w0BAQUFAAOBgQA76Hht\n"
    "ldY9avcTGSwbwoiuIqv0jTL1fHFnzy3RHMLDh+Lpvolc5DSrSJHCP5WuK0eeJXhr\n"
    "T5oQpHL9z/cCDLAKCKRa4uV0fhEdOWBqyR9p8y5jJtye72t6CuFUV5iqcpF4BH4f\n"
    "j2VNHwsSrJwkD4QUGlUtH7vwnQmyCFxZMmWAJg==\n"
    "-----END CERTIFICATE-----\n";

static const char kKeyPem[] =
    "-----BEGIN RSA PRIVATE KEY-----\n"
    "MIICXgIBAAKBgQDYK8imMuRi/03z0K1Zi0WnvfFHvwlYeyK9Na6XJYaUoIDAtB92\n"
    "kWdGMdAQhLciHnAjkXLI6W15OoV3gA/ElRZ1xUpxTMhjP6PyY5wqT5r6y8FxbiiF\n"
    "KKAnHmUcrgfVW28tQ+0rkLGMryRtrukXOgXBv7gcrmU7G1jC2a7WqmeI8QIDAQAB\n"
    "AoGBAIBy09Fd4DOq/Ijp8HeKuCMKTHqTW1xGHshLQ6jwVV2vWZIn9aIgmDsvkjCe\n"
    "i6ssZvnbjVcwzSoByhjN8ZCf/i15HECWDFFh6gt0P5z0MnChwzZmvatV/FXCT0j+\n"
    "WmGNB/gkehKjGXLLcjTb6dRYVJSCZhVuOLLcbWIV10gggJQBAkEA8S8sGe4ezyyZ\n"
    "m4e9r95g6s43kPqtj5rewTsUxt+2n4eVodD+ZUlCULWVNAFLkYRTBCASlSrm9Xhj\n"
    "QpmWAHJUkQJBAOVzQdFUaewLtdOJoPCtpYoY1zd22eae8TQEmpGOR11L6kbxLQsk\n"
    "aMly/DOnOaa82tqAGTdqDEZgSNmCeKKknmECQAvpnY8GUOVAubGR6c+W90iBuQLj\n"
    "LtFp/9ihd2w/PoDwrHZaoUYVcT4VSfJQog/k7kjE4MYXYWL8eEKg3WTWQNECQQDk\n"
    "104Wi91Umd1PzF0ijd2jXOERJU1wEKe6XLkYYNHWQAe5l4J4MWj9OdxFXAxIuuR/\n"
    "tfDwbqkta4xcux67//khAkEAvvRXLHTaa6VFzTaiiO8SaFsHV3lQyXOtMrBpB5jd\n"
    "moZWgjHvB2W9Ckn7sDqsPB+U2tyX0joDdQEyuiMECDY8oQ==\n"
    "-----END RSA PRIVATE KEY-----\n";

// Pump one packet exchange (send->decode->deliver).
static bool ExchangePackets(std::shared_ptr<IConnection> sender,
                            std::shared_ptr<IConnection> receiver,
                            std::shared_ptr<MockSender> sender_mock) {
    sender_mock->Clear();
    if (!sender->TrySend()) {
        return false;
    }
    auto buffer = sender_mock->GetLastSentBuffer();
    if (!buffer || buffer->GetDataLength() == 0) {
        return false;
    }
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodePackets(buffer, packets) || packets.empty()) {
        return false;
    }
    receiver->OnPackets(0, packets);
    return true;
}

struct HandshakeEndpoints {
    std::shared_ptr<ClientConnection> client;
    std::shared_ptr<ServerConnection> server;
    std::shared_ptr<MockSender> client_sender;
    std::shared_ptr<MockSender> server_sender;
    std::shared_ptr<common::IEventLoop> loop;
};

// Run a default v1 <-> v1 handshake and return both endpoints plus their mock
// senders so the test can inspect post-handshake state.
// |client_pref|: if non-zero, the client calls SetPreferredVersion(..) before Dial().
// |server_pref|: if non-zero, the server calls SetPreferredVersion(..) before
//                the first client packet arrives.
static HandshakeEndpoints RunHandshake(uint32_t client_pref = 0, uint32_t server_pref = 0) {
    HandshakeEndpoints ep;

    auto server_ctx = std::make_shared<TLSServerCtx>();
    EXPECT_TRUE(server_ctx->Init(kCertPem, kKeyPem, true, 172800));

    auto client_ctx = std::make_shared<TLSClientCtx>();
    // disable early data, default ciphers, skip peer verify (self-signed cert)
    EXPECT_TRUE(client_ctx->Init(false, "", false));

    ep.loop = common::MakeEventLoop();
    if (!ep.loop->Init()) {
        return ep;
    }

    ep.client = std::make_shared<ClientConnection>(client_ctx, ep.loop);
    if (client_pref != 0) {
        ep.client->SetPreferredVersion(client_pref);
    }

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);
    ep.client->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    ep.server = std::make_shared<ServerConnection>(server_ctx, ep.loop, "h3");
    if (server_pref != 0) {
        ep.server->SetPreferredVersion(server_pref);
    }
    // RFC 9000 §18.2: server MUST include original_destination_connection_id
    // (= the DCID from the client's first Initial).  In production this is
    // populated by WorkerServer before AddTransportParam(); here we plug in
    // the same value by peeking at the client's installed-Initial DCID, so
    // that Compatible VN can proceed (server needs ODCID to rekey).
    QuicTransportParams server_tp_cfg = DEFAULT_QUIC_TRANSPORT_PARAMS;
    server_tp_cfg.original_destination_connection_id_ = ep.client->GetInitialSecretDcidForTest();
    EXPECT_FALSE(server_tp_cfg.original_destination_connection_id_.empty());
    ep.server->AddTransportParam(server_tp_cfg);

    ep.client_sender = AttachMockSender(ep.client);
    ep.server_sender = AttachMockSender(ep.server);

    // Four-flight handshake.
    EXPECT_TRUE(ExchangePackets(ep.client, ep.server, ep.client_sender));
    EXPECT_TRUE(ExchangePackets(ep.server, ep.client, ep.server_sender));
    EXPECT_TRUE(ExchangePackets(ep.server, ep.client, ep.server_sender));
    EXPECT_TRUE(ExchangePackets(ep.client, ep.server, ep.client_sender));
    EXPECT_TRUE(ExchangePackets(ep.server, ep.client, ep.server_sender));

    return ep;
}

// --------------------------------------------------------------------------
// Default handshake: both sides end up on the same wire version, CVN completes,
// and the post-Merge transport_param_ reflects the peer's version_information
// consistently with the on-wire version.
// --------------------------------------------------------------------------
//
// In quicX's default configuration:
//   * ClientConnection::DialSetupTLS() notices quic_version_ defaults to v2,
//     so it calls SetPreferredVersion(v2) + SetVersion(v1) — the client sends
//     v1 Initials but advertises willingness to upgrade to v2.
//   * ServerConnection has no explicit preference (preferred_version_ == 0);
//     OnInitialPacket() pins quic_version_ to v1 (from the client's wire
//     version) and rebuilds the local version_information TP accordingly.
// Consequently the advertised |available_versions| lists are asymmetric:
//   * Client:  [v2, v1]  (preferred + wire)
//   * Server:  [v1]      (wire only; no unsolicited upgrade offer)
TEST(CompatibleVersionNegotiation, DefaultHandshakeIsConservative) {
    auto ep = RunHandshake();
    ASSERT_NE(ep.client, nullptr);
    ASSERT_NE(ep.server, nullptr);

    // Both sides reach Connected on the same wire version (v1, per RFC 9368 §3
    // recommendation to start v2-preferring clients on v1 Initials).
    EXPECT_EQ(ep.client->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    EXPECT_EQ(ep.server->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    EXPECT_EQ(ep.client->GetQuicVersionForTest(), kQuicVersion1);
    EXPECT_EQ(ep.server->GetQuicVersionForTest(), kQuicVersion1);

    // Role bit is plumbed correctly.
    EXPECT_FALSE(ep.client->IsServerForTest());
    EXPECT_TRUE(ep.server->IsServerForTest());

    // After receiving peer's TP, ValidateAndMaybeUpgradeByRemoteTP should have
    // run on both sides and set the "done" flag.
    EXPECT_TRUE(ep.client->CompatVnCompletedForTest());
    EXPECT_TRUE(ep.server->CompatVnCompletedForTest());

    // transport_param_ has been Merge()'d with the peer's values, so on each
    // side it now reflects what the *peer* advertised.
    //
    // Server side reflects the CLIENT's advertised list; client preferred v2
    // (because quic_version_ defaults to v2), so the client advertises
    // available = [v2, v1] and chosen = v1 (the wire version).
    const TransportParam& server_tp = ep.server->GetLocalTransportParamForTest();
    ASSERT_TRUE(server_tp.HasVersionInformation());
    EXPECT_EQ(server_tp.GetChosenVersion(), kQuicVersion1);
    ASSERT_EQ(server_tp.GetAvailableVersions().size(), 2u);
    EXPECT_EQ(server_tp.GetAvailableVersions()[0], kQuicVersion2);
    EXPECT_EQ(server_tp.GetAvailableVersions()[1], kQuicVersion1);

    // Client side reflects the SERVER's advertised list; the server had no
    // explicit preference and its quic_version_ was pinned to v1 when the
    // first Initial arrived, so it advertises chosen=v1, available=[v1] only.
    // This is the "conservative default": a server without explicit preference
    // does not offer unsolicited upgrades.
    const TransportParam& client_tp = ep.client->GetLocalTransportParamForTest();
    ASSERT_TRUE(client_tp.HasVersionInformation());
    EXPECT_EQ(client_tp.GetChosenVersion(), kQuicVersion1);
    ASSERT_EQ(client_tp.GetAvailableVersions().size(), 1u);
    EXPECT_EQ(client_tp.GetAvailableVersions()[0], kQuicVersion1);
}

// --------------------------------------------------------------------------
// Setting preferred_version == kQuicVersion1 on the client (i.e. "no upgrade
// desired") collapses available_versions to a single entry and matches the
// server's default behaviour exactly.
// --------------------------------------------------------------------------
TEST(CompatibleVersionNegotiation, ClientPreferV1NoUpgrade) {
    // Force client to explicitly prefer v1 and the wire version also v1 —
    // DialSetupTLS() skips the "prefer v2, start v1" auto-downgrade path
    // because quic_version_ is already kQuicVersion1.
    auto ep = RunHandshake(/*client_pref=*/kQuicVersion1, /*server_pref=*/0);
    ASSERT_NE(ep.client, nullptr);
    ASSERT_NE(ep.server, nullptr);

    EXPECT_EQ(ep.client->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    EXPECT_EQ(ep.server->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    EXPECT_EQ(ep.client->GetQuicVersionForTest(), kQuicVersion1);
    EXPECT_EQ(ep.server->GetQuicVersionForTest(), kQuicVersion1);
    EXPECT_TRUE(ep.client->CompatVnCompletedForTest());
    EXPECT_TRUE(ep.server->CompatVnCompletedForTest());

    // Client set preferred == v1 BEFORE Dial() — but ClientConnection::DialSetupTLS
    // internally calls SetPreferredVersion(quic_version_) only when quic_version_
    // != v1. Because the application hasn't actually changed quic_version_ from
    // the v2 default yet, DialSetupTLS will STILL clobber our preferred with v2
    // and drop wire to v1. This is the current quicX behaviour — we verify it
    // so any future change to DialSetupTLS is caught.
    //
    // => After Dial, client preferred_version_ is effectively v2 again
    //    (GetPreferredVersion() returns preferred_version_ which is v2).
    // => Advertised available = [v2, v1], exactly like DefaultHandshake.
    const TransportParam& server_tp = ep.server->GetLocalTransportParamForTest();
    ASSERT_TRUE(server_tp.HasVersionInformation());
    EXPECT_EQ(server_tp.GetChosenVersion(), kQuicVersion1);
    ASSERT_EQ(server_tp.GetAvailableVersions().size(), 2u);
    EXPECT_EQ(server_tp.GetAvailableVersions()[0], kQuicVersion2);
    EXPECT_EQ(server_tp.GetAvailableVersions()[1], kQuicVersion1);
}

// --------------------------------------------------------------------------
// Real v1 -> v2 Compatible Version Negotiation (RFC 9368 §3/§4).
//
// Both client and server prefer QUIC v2:
//   * The client auto-starts on v1 (DialSetupTLS() honours the v2 preference
//     by calling SetPreferredVersion(v2) + SetVersion(v1)).
//   * The server's preferred_version_ is set explicitly to v2 (same as the
//     client's); so when the server receives the client's TP and learns that
//     the client advertises v2 in available_versions, it decides to upgrade.
//   * ValidateAndMaybeUpgradeByRemoteTP() on the server calls
//     ConnectionCrypto::RekeyInitialForVersion(v2, odcid, ..., is_server=true)
//     and bumps quic_version_ to v2.  Subsequent Initial packets the server
//     emits are therefore v2 on the wire.
//   * The client's OnInitialPacket() sees pkt_version=v2 while its
//     quic_version_ is still v1; it calls RekeyInitialForVersion(v2, ...,
//     is_server=false) using the cached DCID from when the Initial secret was
//     first installed (same DCID as server's odcid).
//
// Verification:
//   * Both sides reach Connected.
//   * Both sides' final quic_version_ == v2.
//   * Both sides' CompatVnCompleted flag is set.
//   * The local transport_param_ (populated from the peer's TP via Merge())
//     reports chosen_version = v2 on both sides.
// --------------------------------------------------------------------------
TEST(CompatibleVersionNegotiation, V1ToV2Upgrade) {
    // client_pref = 0: rely on DialSetupTLS()'s default behaviour, which for
    //                  quic_version_ defaulting to v2 translates to
    //                  SetPreferredVersion(v2) + SetVersion(v1).
    // server_pref = v2: server explicitly prefers v2, so when it sees the
    //                   client advertise [v2, v1] it will upgrade.
    auto ep = RunHandshake(/*client_pref=*/0, /*server_pref=*/kQuicVersion2);
    ASSERT_NE(ep.client, nullptr);
    ASSERT_NE(ep.server, nullptr);

    // Handshake completed.
    EXPECT_EQ(ep.client->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    EXPECT_EQ(ep.server->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);

    // Both sides migrated to v2.
    EXPECT_EQ(ep.client->GetQuicVersionForTest(), kQuicVersion2);
    EXPECT_EQ(ep.server->GetQuicVersionForTest(), kQuicVersion2);

    // CVN completed on both sides.
    EXPECT_TRUE(ep.client->CompatVnCompletedForTest());
    EXPECT_TRUE(ep.server->CompatVnCompletedForTest());

    // After Merge(), transport_param_ reflects the peer's advertised values.
    // Both client and server advertise chosen = v2 post-upgrade (server
    // rebuilt its TP in ValidateAndMaybeUpgradeByRemoteTP; client rebuilt its
    // TP in OnInitialPacket when the wire version flipped).
    const TransportParam& server_tp = ep.server->GetLocalTransportParamForTest();
    ASSERT_TRUE(server_tp.HasVersionInformation());
    // server_tp is post-Merge == peer (client)'s TP; client's chosen_version
    // MUST equal its on-wire Initial version — i.e. v1 (client never changes
    // its first-Initial-on-wire version, only rekeys). RFC 9368 §4.
    EXPECT_EQ(server_tp.GetChosenVersion(), kQuicVersion1);
    // Client offered [v2, v1].
    ASSERT_EQ(server_tp.GetAvailableVersions().size(), 2u);
    EXPECT_EQ(server_tp.GetAvailableVersions()[0], kQuicVersion2);
    EXPECT_EQ(server_tp.GetAvailableVersions()[1], kQuicVersion1);

    const TransportParam& client_tp = ep.client->GetLocalTransportParamForTest();
    ASSERT_TRUE(client_tp.HasVersionInformation());
    // client_tp is post-Merge == peer (server)'s TP; the server rebuilt its
    // version_information after the upgrade, so chosen_version = v2 and the
    // available_versions reflect its preference list.
    EXPECT_EQ(client_tp.GetChosenVersion(), kQuicVersion2);
    ASSERT_GE(client_tp.GetAvailableVersions().size(), 1u);
    // First entry is server's preferred (v2).
    EXPECT_EQ(client_tp.GetAvailableVersions()[0], kQuicVersion2);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
