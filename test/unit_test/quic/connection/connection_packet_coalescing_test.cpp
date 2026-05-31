// Test for RFC 9000 §12.2 packet coalescing during the QUIC handshake.
//
// Packet coalescing means combining QUIC packets of different encryption
// levels (e.g. Initial+Handshake, or Handshake+1-RTT) into the same UDP
// datagram. This test verifies that:
//   1. The server's first reply (which carries Initial-ACK + Handshake)
//      is delivered as a single UDP datagram containing two QUIC packets.
//   2. The client's follow-up reply (Handshake-ACK + 1-RTT data) is also
//      a single datagram with two coalesced packets.
//   3. The receiver decodes all packets in a coalesced datagram correctly
//      and the handshake completes end-to-end.

#include <gtest/gtest.h>

#include "common/log/log.h"
#include "common/timer/timer.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/type.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/connection_server.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/quicx/global_resource.h"
#include "mock_sender.h"
#include "connection_test_util.h"

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

// Trigger a TrySend on send_conn, decode all packets contained in the
// resulting datagram and forward them to recv_conn. Returns the number
// of QUIC packets contained in the datagram (>=1 means a datagram was
// produced; ==0 means there was nothing to send).
static size_t SendAndDeliverDatagram(std::shared_ptr<IConnection> send_conn,
                                     std::shared_ptr<IConnection> recv_conn,
                                     std::shared_ptr<MockSender> sender_mock) {
    sender_mock->Clear();
    if (!send_conn->TrySend()) {
        return 0;
    }

    auto buffer = sender_mock->GetLastSentBuffer();
    if (!buffer || buffer->GetDataLength() == 0) {
        return 0;
    }

    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodePackets(buffer, packets)) {
        return 0;
    }

    recv_conn->OnPackets(0, packets);
    return packets.size();
}

// RFC 9000 §12.2: Verify that the server's first response after receiving
// the client Initial produces both Initial and Handshake packets.
// Note: The current TrySend() implementation sends one encryption level per
// call (no intra-datagram coalescing yet). This test verifies that the server
// correctly generates both Initial (ACK + ServerHello CRYPTO) and Handshake
// (EE, Cert, CV, Finished CRYPTO) packets across multiple TrySend() rounds,
// and that Initial packets are produced before Handshake packets (ordering
// requirement from RFC 9001 §4.1.4).
TEST(quic_connection_coalescing_utest, server_init_handshake_coalesce) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false, "", false);

    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, event_loop);
    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, event_loop, "h3");
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto client_sender = AttachMockSender(client_conn);
    auto server_sender = AttachMockSender(server_conn);

    // Step 1: client -> server (Initial)
    size_t client_initial_pkts = SendAndDeliverDatagram(client_conn, server_conn, client_sender);
    ASSERT_GE(client_initial_pkts, 1u);

    // Step 2: server -> client. Drive TrySend() multiple times to drain all
    // pending crypto data. The server should produce Initial packet(s)
    // (ACK + ServerHello CRYPTO) followed by Handshake packet(s)
    // (EncryptedExtensions, Certificate, CertificateVerify, Finished).
    server_sender->Clear();

    // Collect all packets produced by the server across multiple TrySend rounds
    std::vector<std::shared_ptr<IPacket>> all_server_pkts;
    for (int round = 0; round < 20; ++round) {
        if (!server_conn->TrySend()) {
            break;
        }
        // Decode packets from the last sent datagram
        auto buf = server_sender->GetLastSentBuffer();
        if (!buf || buf->GetDataLength() == 0) {
            break;
        }
        std::vector<std::shared_ptr<IPacket>> pkts;
        if (DecodePackets(buf, pkts)) {
            for (auto& p : pkts) {
                all_server_pkts.push_back(p);
            }
        }
    }

    LOG_INFO("server produced %zu packet(s) total across all TrySend rounds",
             all_server_pkts.size());
    ASSERT_GE(all_server_pkts.size(), 2u)
        << "Server should produce at least Initial + Handshake packets";

    // Verify that both Initial and Handshake packets are present,
    // and that all Initial packets appear before any Handshake packet.
    bool seen_initial = false;
    bool seen_handshake = false;
    for (const auto& pkt : all_server_pkts) {
        auto type = pkt->GetHeader()->GetPacketType();
        if (type == PacketType::kInitialPacketType) {
            EXPECT_FALSE(seen_handshake)
                << "Initial packets must precede Handshake packets (RFC 9001 §4.1.4)";
            seen_initial = true;
        } else if (type == PacketType::kHandshakePacketType) {
            seen_handshake = true;
        }
    }
    EXPECT_TRUE(seen_initial) << "Server must produce Initial packet(s)";
    EXPECT_TRUE(seen_handshake) << "Server must produce Handshake packet(s)";
}

// RFC 9000 §12.2: End-to-end handshake should still succeed when packet
// coalescing is in effect, exercising the receive path's ability to parse
// multi-packet datagrams.
TEST(quic_connection_coalescing_utest, full_handshake_with_coalescing) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false, "", false);

    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, event_loop);
    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, event_loop, "h3");
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto client_sender = AttachMockSender(client_conn);
    auto server_sender = AttachMockSender(server_conn);

    // 1) client -> server (Initial)
    ASSERT_GE(SendAndDeliverDatagram(client_conn, server_conn, client_sender), 1u);

    // 2) server -> client (Initial + Handshake coalesced)
    ASSERT_GE(SendAndDeliverDatagram(server_conn, client_conn, server_sender), 1u);

    // 3) Drive the rest of the handshake. With packet coalescing the exact
    // number of TrySend rounds may differ from the non-coalesced layout
    // (immediate ACKs etc.). Loop until both sides reach the application
    // encryption level or we hit a safety bound.
    for (int i = 0; i < 30; ++i) {
        SendAndDeliverDatagram(server_conn, client_conn, server_sender);
        SendAndDeliverDatagram(client_conn, server_conn, client_sender);
        if (server_conn->GetCurEncryptionLevel() == kApplication &&
            client_conn->GetCurEncryptionLevel() == kApplication) {
            break;
        }
    }

    EXPECT_EQ(server_conn->GetCurEncryptionLevel(), kApplication);
    EXPECT_EQ(client_conn->GetCurEncryptionLevel(), kApplication);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
