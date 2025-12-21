#include <gtest/gtest.h>

#include "common/log/log.h"
#include "common/timer/timer.h"
#include "quic/packet/packet_decode.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/include/if_quic_send_stream.h"
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

using quicx::quic::ConnectionProcess;
using quicx::quic::AttachMockSender;

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

// Note: ConnectionProcess is defined in connection_test_util.h

TEST(quic_connection_utest, handshake) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto client_conn = std::make_shared<ClientConnection>(client_ctx, event_loop, nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, event_loop, "h3", nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // Attach MockSenders
    auto client_sender = AttachMockSender(client_conn);
    auto server_sender = AttachMockSender(server_conn);

    // client -------init-----> server
    ConnectionProcess(client_conn, server_conn, client_sender);
    // client <------init------ server
    ConnectionProcess(server_conn, client_conn, server_sender);
    // client <---handshake---- server
    ConnectionProcess(server_conn, client_conn, server_sender);
    // client ----handshake---> server
    ConnectionProcess(client_conn, server_conn, client_sender);
    // client <----session----- server
    ConnectionProcess(server_conn, client_conn, server_sender);

    EXPECT_EQ(server_conn->GetCurEncryptionLevel(), kApplication);
    EXPECT_EQ(client_conn->GetCurEncryptionLevel(), kApplication);

    // Process post-handshake messages to capture session ticket (similar to successful bssl_0rtt_demo.cpp)
    // This ensures the session ticket with 0-RTT capability is properly captured
    for (int i = 0; i < 10; ++i) {
        // Try to process any remaining handshake data
        server_sender->Clear();
        if (server_conn->TrySend()) {
            auto buffer = server_sender->GetLastSentBuffer();
            if (buffer && buffer->GetDataLength() > 0) {
                std::vector<std::shared_ptr<IPacket>> pkts;
                if (DecodePackets(buffer, pkts)) {
                    for (auto& pkt : pkts) {
                        std::vector<std::shared_ptr<IPacket>> pkt_vec = {pkt};
                        client_conn->OnPackets(0, pkt_vec);
                    }
                }
            }
        }
        
        // Also try client side
        client_sender->Clear();
        if (client_conn->TrySend()) {
            auto buffer = client_sender->GetLastSentBuffer();
            if (buffer && buffer->GetDataLength() > 0) {
                std::vector<std::shared_ptr<IPacket>> pkts;
                if (DecodePackets(buffer, pkts)) {
                    for (auto& pkt : pkts) {
                        std::vector<std::shared_ptr<IPacket>> pkt_vec = {pkt};
                        server_conn->OnPackets(0, pkt_vec);
                    }
                }
            }
        }
    }

    std::string session_der;
    ASSERT_TRUE(client_conn->ExportResumptionSession(session_der));
    ASSERT_FALSE(session_der.empty());
    common::LOG_DEBUG("session_der: %s, size: %zu", session_der.c_str(), session_der.size());
}

TEST(quic_connection_utest, resume_0rtt_basic) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(true);

    // 1) First connection: full handshake to obtain resumption session
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto client_conn = std::make_shared<ClientConnection>(client_ctx, event_loop, nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, event_loop, "h3", nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // Attach MockSenders for first connection
    auto client_sender = AttachMockSender(client_conn);
    auto server_sender = AttachMockSender(server_conn);

    // client -------init-----> server
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn, client_sender));
    // client <------init------ server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn, server_sender));
    // client <---handshake---- server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn, server_sender));
    // client ----handshake---> server
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn, client_sender));
    // client <----session----- server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn, server_sender));

    EXPECT_EQ(server_conn->GetCurEncryptionLevel(), kApplication);
    EXPECT_EQ(client_conn->GetCurEncryptionLevel(), kApplication);

    std::string session_der;
    ASSERT_TRUE(client_conn->ExportResumptionSession(session_der));
    ASSERT_FALSE(session_der.empty());
    common::LOG_DEBUG("session_der: %s, size: %zu", session_der.c_str(), session_der.size());

    // 2) Second connection: provide session to enable 0-RTT and send early data
    auto event_loop2 = common::MakeEventLoop();
    ASSERT_TRUE(event_loop2->Init());
    auto client_conn2 = std::make_shared<ClientConnection>(client_ctx, event_loop2, nullptr, nullptr, nullptr, nullptr, nullptr);

    client_conn2->Dial(addr, "h3", session_der, DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn2 = std::make_shared<ServerConnection>(server_ctx, event_loop2, "h3", nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn2->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // Attach MockSender for second connection
    auto client_sender2 = AttachMockSender(client_conn2);
    auto server_sender2 = AttachMockSender(server_conn2);

    // queue early application data before handshake completes
    auto s_base = client_conn2->MakeStream(StreamDirection::kSend);
    auto s = std::dynamic_pointer_cast<IQuicSendStream>(s_base);
    ASSERT_NE(s, nullptr);
    const char* early = "hello 0rtt";
    ASSERT_GT(s->Send((uint8_t*)early, (uint32_t)strlen(early)), 0);

    // First flight from client should be Initial or 0-RTT (if session supports it)
    client_sender2->Clear();
    ASSERT_TRUE(client_conn2->TrySend());
    auto buffer1 = client_sender2->GetLastSentBuffer();
    ASSERT_NE(buffer1, nullptr);
    ASSERT_GT(buffer1->GetDataLength(), 0);
    std::vector<std::shared_ptr<IPacket>> pkts1;
    ASSERT_TRUE(DecodePackets(buffer1, pkts1));
    ASSERT_FALSE(pkts1.empty());
    
    // In 0-RTT scenario, the first packet might be 0-RTT instead of Initial
    auto packet_type = pkts1[0]->GetHeader()->GetPacketType();
    EXPECT_TRUE(packet_type == PacketType::kInitialPacketType || 
                packet_type == PacketType::k0RttPacketType);

    // Deliver to server to let it process ClientHello and set up 0-RTT keys
    server_conn2->OnPackets(0, pkts1);

    // Next flight from client should contain 0-RTT (if keys available and stream data queued)
    bool found_0rtt = false;
    for (int i = 0; i < 4 && !found_0rtt; ++i) {
        client_sender2->Clear();
        if (!client_conn2->TrySend()) {
            // If no more data to send, break
            break;
        }
        auto buffern = client_sender2->GetLastSentBuffer();
        if (!buffern || buffern->GetDataLength() == 0) {
            break;
        }
        std::vector<std::shared_ptr<IPacket>> pktsn;
        ASSERT_TRUE(DecodePackets(buffern, pktsn));
        for (auto& p : pktsn) {
            if (p->GetHeader()->GetPacketType() == PacketType::k0RttPacketType) {
                found_0rtt = true;
                break;
            }
        }
        // feed to server to advance state even if 0-RTT not yet present
        if (!pktsn.empty()) server_conn2->OnPackets(0, pktsn);
    }
    EXPECT_TRUE(found_0rtt);
    // Drain remaining handshake flights in both directions until server reaches application level
    for (int i = 0; i < 10 && server_conn2->GetCurEncryptionLevel() != kApplication; ++i) {
        // server -> client
        {
            server_sender2->Clear();
            if (server_conn2->TrySend()) {
                auto buffer = server_sender2->GetLastSentBuffer();
                if (buffer && buffer->GetDataLength() > 0) {
                    std::vector<std::shared_ptr<IPacket>> pkts;
                    if (DecodePackets(buffer, pkts) && !pkts.empty()) {
                        client_conn2->OnPackets(0, pkts);
                    }
                }
            }
        }
        // client -> server
        {
            client_sender2->Clear();
            if (client_conn2->TrySend()) {
                auto buffer = client_sender2->GetLastSentBuffer();
                if (buffer && buffer->GetDataLength() > 0) {
                    std::vector<std::shared_ptr<IPacket>> pkts;
                    if (DecodePackets(buffer, pkts) && !pkts.empty()) {
                        server_conn2->OnPackets(0, pkts);
                    }
                }
            }
        }
    }
    EXPECT_EQ(server_conn2->GetCurEncryptionLevel(), kApplication);
}

TEST(quic_connection_utest, reject_0rtt_basic) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(true);

    // 1) First connection: full handshake to obtain resumption session
    auto event_loop = common::MakeEventLoop();
    ASSERT_TRUE(event_loop->Init());
    auto client_conn = std::make_shared<ClientConnection>(client_ctx, event_loop, nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, event_loop, "h3", nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // Attach MockSenders for first connection
    auto client_sender = AttachMockSender(client_conn);
    auto server_sender = AttachMockSender(server_conn);

    // client -------init-----> server
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn, client_sender));
    // client <------init------ server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn, server_sender));
    // client <---handshake---- server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn, server_sender));
    // client ----handshake---> server
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn, client_sender));
    // client <----session----- server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn, server_sender));

    EXPECT_EQ(server_conn->GetCurEncryptionLevel(), kApplication);
    EXPECT_EQ(client_conn->GetCurEncryptionLevel(), kApplication);

    std::string session_der;
    ASSERT_TRUE(client_conn->ExportResumptionSession(session_der));
    ASSERT_FALSE(session_der.empty());
    common::LOG_DEBUG("session_der: %s, size: %zu", session_der.c_str(), session_der.size());

    // 2) Second connection: provide session to enable 0-RTT and send early data
    auto event_loop2 = common::MakeEventLoop();
    ASSERT_TRUE(event_loop2->Init());
    auto client_conn2 = std::make_shared<ClientConnection>(client_ctx, event_loop2, nullptr, nullptr, nullptr, nullptr, nullptr);

    client_conn2->Dial(addr, "h3", session_der, DEFAULT_QUIC_TRANSPORT_PARAMS);


    std::shared_ptr<TLSServerCtx> server_ctx_2 = std::make_shared<TLSServerCtx>();
    server_ctx_2->Init(kCertPem, kKeyPem, false, 172800);

    auto server_conn2 = std::make_shared<ServerConnection>(server_ctx_2, event_loop2, "h3", nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn2->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // Attach MockSender for second connection
    auto client_sender2 = AttachMockSender(client_conn2);
    auto server_sender2 = AttachMockSender(server_conn2);

    // queue early application data before handshake completes
    auto s_base = client_conn2->MakeStream(StreamDirection::kSend);
    auto s = std::dynamic_pointer_cast<IQuicSendStream>(s_base);
    ASSERT_NE(s, nullptr);
    const char* early = "hello 0rtt";
    ASSERT_GT(s->Send((uint8_t*)early, (uint32_t)strlen(early)), 0);

    // First flight from client should be Initial or 0-RTT (if session supports it)
    client_sender2->Clear();
    ASSERT_TRUE(client_conn2->TrySend());
    auto buffer1 = client_sender2->GetLastSentBuffer();
    ASSERT_NE(buffer1, nullptr);
    ASSERT_GT(buffer1->GetDataLength(), 0);
    std::vector<std::shared_ptr<IPacket>> pkts1;
    ASSERT_TRUE(DecodePackets(buffer1, pkts1));
    ASSERT_FALSE(pkts1.empty());
    
    // In 0-RTT scenario, the first packet might be 0-RTT instead of Initial
    auto packet_type = pkts1[0]->GetHeader()->GetPacketType();
    EXPECT_TRUE(packet_type == PacketType::kInitialPacketType || 
                packet_type == PacketType::k0RttPacketType);

    // Deliver to server to let it process ClientHello and set up 0-RTT keys
    server_conn2->OnPackets(0, pkts1);

    // Next flight from client should contain 0-RTT (if keys available and stream data queued)
    bool found_0rtt = false;
    for (int i = 0; i < 5 && !found_0rtt; ++i) {
        client_sender2->Clear();
        if (!client_conn2->TrySend()) {
            // If no more data to send, break
            break;
        }
        auto buffern = client_sender2->GetLastSentBuffer();
        if (!buffern || buffern->GetDataLength() == 0) {
            break;
        }
        std::vector<std::shared_ptr<IPacket>> pktsn;
        ASSERT_TRUE(DecodePackets(buffern, pktsn));
        for (auto& p : pktsn) {
            if (p->GetHeader()->GetPacketType() == PacketType::k0RttPacketType) {
                found_0rtt = true;
                break;
            }
        }
        // feed to server to advance state even if 0-RTT not yet present
        if (!pktsn.empty()) server_conn2->OnPackets(0, pktsn);
    }
    EXPECT_TRUE(found_0rtt);
    EXPECT_NE(server_conn2->GetCurEncryptionLevel(), kApplication);
}

}
}
}