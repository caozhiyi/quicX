#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "connection_test_util.h"
#include "mock_sender.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/connection_server.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/packet/packet_decode.h"
#include "quic/quicx/global_resource.h"

// RFC 9000 §8.1 (Address Validation during Connection Establishment):
//
//   "...a server MUST NOT send more than three times as many bytes as the
//    number of bytes it has received prior to validating the client's address."
//
// The controller implementing this rule already had thorough unit tests
// (anti_amplification_controller_test.cpp), and they all passed -- while the
// connection never actually entered the unvalidated state and nothing ever
// called into the controller from the send path. Testing the controller in
// isolation cannot catch that: the object under test was correct, it was simply
// not plugged in. Everything here therefore drives a *real* ServerConnection and
// measures bytes that actually left through the sender.

namespace quicx {
namespace quic {
namespace {

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

// A client and a server wired to MockSenders, with the server put in exactly the
// state ServerWorker leaves it in for a connection that arrived without a
// validated Retry token.
struct SpoofedHandshake {
    std::shared_ptr<common::IEventLoop> event_loop;
    std::shared_ptr<ClientConnection> client;
    std::shared_ptr<ServerConnection> server;
    std::shared_ptr<MockSender> client_sender;
    std::shared_ptr<MockSender> server_sender;
};

SpoofedHandshake MakePeers() {
    SpoofedHandshake p;

    auto server_ctx = std::make_shared<TLSServerCtx>();
    EXPECT_TRUE(server_ctx->Init(kCertPem, kKeyPem, true, 172800));

    auto client_ctx = std::make_shared<TLSClientCtx>();
    EXPECT_TRUE(client_ctx->Init(false, "", false));

    p.event_loop = common::MakeEventLoop();
    EXPECT_TRUE(p.event_loop->Init());

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    p.client = std::make_shared<ClientConnection>(client_ctx, p.event_loop);
    p.client->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    p.server = std::make_shared<ServerConnection>(server_ctx, p.event_loop, "h3");
    p.server->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    p.client_sender = AttachMockSender(p.client);
    p.server_sender = AttachMockSender(p.server);

    // Deliberately no EnterUnvalidatedAddressState() here: the budget is on by
    // construction, and these tests are the ones that must notice if that stops
    // being true. Calling it explicitly would make them pass even if the
    // production accept path forgot to restrict the connection.
    return p;
}

// Total datagram bytes captured so far. This is the quantity RFC 9000 §8.1
// bounds, so it is what the test must count -- not packet counts, not frame
// payloads.
uint64_t TotalBytesSent(const std::shared_ptr<MockSender>& sender) {
    uint64_t total = 0;
    for (const auto& pkt : sender->GetSentPackets()) {
        auto buf = pkt->GetData();
        if (buf) {
            total += buf->GetDataLength();
        }
    }
    return total;
}

// Deliver one datagram, returning how many bytes the receiver was handed.
uint64_t Deliver(const std::shared_ptr<common::IBuffer>& datagram, const std::shared_ptr<IConnection>& to) {
    if (!datagram || datagram->GetDataLength() == 0) {
        return 0;
    }
    const uint64_t bytes = datagram->GetDataLength();
    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodePackets(datagram, packets)) {
        return 0;
    }
    to->OnPackets(0, packets);
    return bytes;
}

// Pump a connection until it stops producing datagrams (or the cap trips).
void PumpUntilQuiet(const std::shared_ptr<IConnection>& conn, int max_rounds = 64) {
    for (int i = 0; i < max_rounds; ++i) {
        if (conn->TrySendBurst(4) == 0) {
            return;
        }
    }
}

// ---------------------------------------------------------------------------

// The original defect: AntiAmplificationController was constructed but
// EnterUnvalidatedState() had no callers, so is_unvalidated_ was false for the
// entire life of every connection and CanSend() waved everything through.
// MarkAddressValidated() likewise had no callers. This pins both ends.
TEST(AntiAmplificationIntegrationTest, server_is_amp_limited_until_handshake_arrives) {
    auto p = MakePeers();
    const auto& amp = p.server->GetSendManagerForTest().GetAmpControllerForTest();

    // Before anything arrives: restricted, and with zero credit. A non-zero
    // starting credit here would be free amplification for the attacker, which
    // is why the server uses ResetAmpBudget(0) rather than the probing default.
    ASSERT_TRUE(amp.IsUnvalidated());
    EXPECT_EQ(amp.GetBytesReceived(), 0u);
    EXPECT_EQ(amp.GetRemainingBudget(), 0u);

    // client Initial -> server. Still unvalidated: an Initial proves nothing,
    // anyone can spoof one.
    ASSERT_GT(DrainInto(p.client, p.server, p.client_sender), 0);
    EXPECT_TRUE(amp.IsUnvalidated());
    EXPECT_GT(amp.GetBytesReceived(), 0u);

    // server Initial+Handshake -> client
    ASSERT_GT(DrainInto(p.server, p.client, p.server_sender), 0);
    EXPECT_TRUE(amp.IsUnvalidated());

    // client Handshake -> server. Decrypting a Handshake packet proves the peer
    // received our Initial secrets, i.e. it really is at that address.
    ASSERT_GT(DrainInto(p.client, p.server, p.client_sender), 0);
    EXPECT_FALSE(amp.IsUnvalidated()) << "handshake packet must lift the §8.1 budget";
    EXPECT_FALSE(p.server->GetSendManagerForTest().IsAmpBlocked());
}

// The attack this rule exists to stop: one spoofed Initial carrying a victim's
// source address, attacker then goes silent. Whatever the server emits is
// aimed at the victim, so it must stay within 3x what the attacker spent.
TEST(AntiAmplificationIntegrationTest, forged_initial_cannot_amplify) {
    auto p = MakePeers();
    const auto& amp = p.server->GetSendManagerForTest().GetAmpControllerForTest();

    // Attacker's single spoofed datagram.
    p.client_sender->Clear();
    ASSERT_GT(p.client->TrySendBurst(1), 0u);
    auto initial = p.client_sender->GetLastSentBuffer();
    ASSERT_NE(initial, nullptr);
    const uint64_t received = Deliver(initial, p.server);
    ASSERT_GT(received, 0u);

    // Server is pumped as hard as it will go. The victim never answers, so no
    // further credit ever arrives.
    p.server_sender->Clear();
    PumpUntilQuiet(p.server);
    const uint64_t sent = TotalBytesSent(p.server_sender);

    EXPECT_GT(sent, 0u) << "server should still answer, just within budget";
    EXPECT_LE(sent, 3 * received) << "amplification: received " << received << " B, sent " << sent << " B (ratio "
                                  << (static_cast<double>(sent) / static_cast<double>(received)) << "x)";
    EXPECT_TRUE(amp.IsUnvalidated()) << "address was never proven, budget must stay on";
}

// The budget must clamp the *egress path*, not merely be tracked alongside it.
// Sizing the credit below the server's pending flight makes the clamp load-
// bearing regardless of how large the certificate chain happens to be -- with a
// small test cert the whole flight fits inside 3x1200 and the assertion above,
// while correct, would not actually bite.
TEST(AntiAmplificationIntegrationTest, emitter_clamps_flight_to_budget) {
    auto p = MakePeers();
    auto& send_manager = p.server->GetSendManagerForTest();
    const auto& amp = send_manager.GetAmpControllerForTest();

    // Let the server build a real Initial+Handshake flight.
    p.client_sender->Clear();
    ASSERT_GT(p.client->TrySendBurst(1), 0u);
    ASSERT_GT(Deliver(p.client_sender->GetLastSentBuffer(), p.server), 0u);

    // Now model a stingier attacker: only 100 bytes of credit, so at most 300
    // bytes may leave. Re-arming resets both counters.
    const uint64_t kCredit = 100;
    send_manager.ResetAmpBudget(kCredit);
    ASSERT_TRUE(amp.IsUnvalidated());
    ASSERT_EQ(amp.GetRemainingBudget(), 3 * kCredit);

    p.server_sender->Clear();
    PumpUntilQuiet(p.server);
    const uint64_t clamped = TotalBytesSent(p.server_sender);

    EXPECT_LE(clamped, 3 * kCredit) << "emitter let " << clamped << " B through on a " << (3 * kCredit) << " B budget";
    EXPECT_TRUE(send_manager.IsAmpBlocked()) << "server has more to send and must report itself blocked, "
                                                "otherwise the worker spins on it";

    // Validation lifts the gate: the same egress path that just refused 1146 B
    // now accepts a full-size datagram.
    //
    // Note this asserts the *gate*, not immediate re-emission. A datagram
    // refused here has already passed through SendControl::OnPacketSend (it is
    // in unacked_packets with a PTO armed) and its CRYPTO frames have been
    // drained from the stream, so the withheld flight comes back on PTO rather
    // than the instant the address is proven. RFC 9002 §6.2.2.1 contemplates
    // exactly that -- an amplification-limited server leans on the timer -- so
    // the data is delayed, never dropped.
    send_manager.MarkAddressValidated();
    EXPECT_FALSE(amp.IsUnvalidated());
    EXPECT_FALSE(send_manager.IsAmpBlocked());
    EXPECT_TRUE(send_manager.CheckAndChargeAmpBudget(1200)) << "budget must be uncapped once the address is validated";
}

}  // namespace
}  // namespace quic
}  // namespace quicx
