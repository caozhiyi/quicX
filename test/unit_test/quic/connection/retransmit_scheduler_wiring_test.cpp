#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "common/util/time.h"

#include "quic/connection/connection_client.h"
#include "quic/connection/connection_server.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/packet/packet_decode.h"
#include "quic/quicx/global_resource.h"

#include "connection_test_util.h"
#include "mock_sender.h"

// P0-C10 / X3 wiring. RetransmitScheduler and CryptoStream's owner role each
// have thorough isolated tests (retransmit_scheduler_test, crypto_stream_
// slowpath_test) -- and every one of them hand-wires the pieces together. The
// production wiring (SendControl owns the scheduler, the connection hands it to
// its CryptoStream) was missing entirely when those tests were green: the
// object under test was correct, it was simply not plugged in. So everything
// here drives REAL Client/ServerConnection objects.

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

struct Peers {
    std::shared_ptr<common::IEventLoop> event_loop;
    std::shared_ptr<ClientConnection> client;
    std::shared_ptr<ServerConnection> server;
    std::shared_ptr<MockSender> client_sender;
    std::shared_ptr<MockSender> server_sender;
};

Peers MakePeers() {
    Peers p;
    auto server_ctx = std::make_shared<TLSServerCtx>();
    EXPECT_TRUE(server_ctx->Init(kCertPem, kKeyPem, true, 172800));
    auto client_ctx = std::make_shared<TLSClientCtx>();
    EXPECT_TRUE(client_ctx->Init(false, "", false));

    p.event_loop = common::MakeEventLoop();
    EXPECT_TRUE(p.event_loop->Init());

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9433);

    p.client = std::make_shared<ClientConnection>(client_ctx, p.event_loop);
    p.client->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);
    p.server = std::make_shared<ServerConnection>(server_ctx, p.event_loop, "h3");
    p.server->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);
    p.client_sender = AttachMockSender(p.client);
    p.server_sender = AttachMockSender(p.server);
    return p;
}

uint64_t BytesOf(const std::shared_ptr<MockSender>& s) {
    uint64_t total = 0;
    for (const auto& pkt : s->GetSentPackets()) {
        if (auto buf = pkt->GetData()) total += buf->GetDataLength();
    }
    return total;
}

TEST(RetransmitSchedulerWiringTest, ClientHelloOutstandingArmsTheInitialSlotOnTheRealScheduler) {
    auto p = MakePeers();
    auto sched = p.client->GetSendManagerForTest().GetSendControl().GetRetransmitScheduler().lock();
    ASSERT_NE(sched, nullptr) << "SendControl must own a scheduler";
    EXPECT_EQ(sched->Size(), 0u) << "nothing outstanding before the first flight";

    // The ClientHello leaves in an Initial packet.
    p.client_sender->Clear();
    ASSERT_GT(p.client->TrySendBurst(1), 0);
    ASSERT_GT(BytesOf(p.client_sender), 0u);

    // The connection must have handed its CryptoStream the scheduler: a sent,
    // unacknowledged Initial CRYPTO flight owns a slot. If this is 0 the slow
    // path is inert and a lost ClientHello is recovered only by the
    // exponentially backing-off connection PTO (lesson L1).
    EXPECT_EQ(sched->Size(), 1u) << "the outstanding ClientHello must be registered with the slow-path scheduler";
    EXPECT_GT(sched->NextDeadlineMs(), common::MonotonicTimeMsec());
}

TEST(RetransmitSchedulerWiringTest, ServerFlightOutstandingArmsSlotsOnTheRealScheduler) {
    auto p = MakePeers();
    auto sched = p.server->GetSendManagerForTest().GetSendControl().GetRetransmitScheduler().lock();
    ASSERT_NE(sched, nullptr);

    p.client_sender->Clear();
    ASSERT_GT(p.client->TrySendBurst(1), 0);
    auto initial = p.client_sender->GetLastSentBuffer();
    ASSERT_NE(initial, nullptr);
    std::vector<std::shared_ptr<IPacket>> packets;
    const uint64_t bytes = initial->GetDataLength();
    ASSERT_TRUE(DecodePackets(initial, packets));
    p.server->OnPackets(0, packets, bytes);

    p.server_sender->Clear();
    for (int i = 0; i < 16 && p.server->TrySendBurst(4) > 0; ++i) {
    }
    ASSERT_GT(BytesOf(p.server_sender), 0u) << "server must answer the ClientHello";

    // ServerHello (Initial) and the certificate flight (Handshake) are both
    // outstanding, each its own slot.
    EXPECT_GE(sched->Size(), 1u) << "the server's unacknowledged handshake flight must be on the slow path";
}

TEST(RetransmitSchedulerWiringTest, UnansweredClientHelloIsRetransmittedThroughTheRealSendPath) {
    auto p = MakePeers();
    auto sched = p.client->GetSendManagerForTest().GetSendControl().GetRetransmitScheduler().lock();
    ASSERT_NE(sched, nullptr);

    p.client_sender->Clear();
    ASSERT_GT(p.client->TrySendBurst(1), 0);
    ASSERT_EQ(sched->Size(), 1u);
    const size_t first_count = p.client_sender->GetSentPackets().size();

    // The server never answers. Fire the slow path as its timer would at the
    // deadline (RunDue is the timer-callback body), then let the client send.
    sched->RunDue(sched->NextDeadlineMs());
    p.client_sender->Clear();
    ASSERT_GT(p.client->TrySendBurst(4), 0) << "the probe must re-queue the ClientHello for the normal send path";

    EXPECT_GE(p.client_sender->GetSentPackets().size(), 1u);
    EXPECT_GE(first_count, 1u);
    // The retransmission is a client Initial datagram: it must still honour
    // RFC 9000 14.1 (>= 1200 B). The retired whole-packet path needed a bespoke
    // PadInitialToDatagramFloor for this; the normal path must do it itself.
    auto last = p.client_sender->GetLastSentBuffer();
    ASSERT_NE(last, nullptr);
    EXPECT_GE(last->GetDataLength(), 1200u) << "retransmitted client Initial must stay >= 1200 bytes (RFC 9000 14.1)";
    // And the slot stays registered: arithmetic cadence continues until acked.
    EXPECT_EQ(sched->Size(), 1u);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
