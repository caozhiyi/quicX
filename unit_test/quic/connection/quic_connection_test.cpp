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

bool ConnectionProcess(std::shared_ptr<IConnection> send_conn, std::shared_ptr<IConnection> recv_conn) {
    std::shared_ptr<common::SingleBlockBuffer> buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1500));
    quic::SendOperation send_operation;
    send_conn->GenerateSendData(buffer, send_operation);

    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodePackets(buffer, packets)) {
        return false;
    }

    recv_conn->OnPackets(0, packets);
    return true;
}

TEST(quic_connection_utest, handshake) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);
    
    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // client -------init-----> server
    ConnectionProcess(client_conn, server_conn);
    // client <------init------ server
    ConnectionProcess(server_conn, client_conn);
    // client <---handshake---- server
    ConnectionProcess(server_conn, client_conn);
    // client ----handshake---> server
    ConnectionProcess(client_conn, server_conn);
    // client <----session----- server
    ConnectionProcess(server_conn, client_conn);

    EXPECT_EQ(server_conn->GetCurEncryptionLevel(), kApplication);
    EXPECT_EQ(client_conn->GetCurEncryptionLevel(), kApplication);

    // Process post-handshake messages to capture session ticket (similar to successful bssl_0rtt_demo.cpp)
    // This ensures the session ticket with 0-RTT capability is properly captured
    for (int i = 0; i < 10; ++i) {
        // Try to process any remaining handshake data
        std::shared_ptr<common::SingleBlockBuffer> buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1500));
        quic::SendOperation op;
        if (server_conn->GenerateSendData(buffer, op)) {
            std::vector<std::shared_ptr<IPacket>> pkts;
            if (DecodePackets(buffer, pkts)) {
                for (auto& pkt : pkts) {
                    std::vector<std::shared_ptr<IPacket>> pkt_vec = {pkt};
                    client_conn->OnPackets(0, pkt_vec);
                }
            }
        }
        
        // Also try client side
        if (client_conn->GenerateSendData(buffer, op)) {
            std::vector<std::shared_ptr<IPacket>> pkts;
            if (DecodePackets(buffer, pkts)) {
                for (auto& pkt : pkts) {
                    std::vector<std::shared_ptr<IPacket>> pkt_vec = {pkt};
                    server_conn->OnPackets(0, pkt_vec);
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
    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // client -------init-----> server
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    // client <------init------ server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    // client <---handshake---- server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    // client ----handshake---> server
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    // client <----session----- server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));

    EXPECT_EQ(server_conn->GetCurEncryptionLevel(), kApplication);
    EXPECT_EQ(client_conn->GetCurEncryptionLevel(), kApplication);

    std::string session_der;
    ASSERT_TRUE(client_conn->ExportResumptionSession(session_der));
    ASSERT_FALSE(session_der.empty());
    common::LOG_DEBUG("session_der: %s, size: %zu", session_der.c_str(), session_der.size());

    // 2) Second connection: provide session to enable 0-RTT and send early data
    auto client_conn2 = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    client_conn2->Dial(addr, "h3", session_der, DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn2 = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn2->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // queue early application data before handshake completes
    auto s_base = client_conn2->MakeStream(StreamDirection::kSend);
    auto s = std::dynamic_pointer_cast<IQuicSendStream>(s_base);
    ASSERT_NE(s, nullptr);
    const char* early = "hello 0rtt";
    ASSERT_GT(s->Send((uint8_t*)early, (uint32_t)strlen(early)), 0);

    // First flight from client should be Initial or 0-RTT (if session supports it)
    std::shared_ptr<common::SingleBlockBuffer> buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1500));
    quic::SendOperation op1;
    ASSERT_TRUE(client_conn2->GenerateSendData(buffer1, op1));
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
        auto buffern = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1500));
        quic::SendOperation opn;
        if (!client_conn2->GenerateSendData(buffern, opn)) {
            // If no more data to send, break
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
            auto buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1500));
            quic::SendOperation op;
            if (server_conn2->GenerateSendData(buffer, op)) {
                std::vector<std::shared_ptr<IPacket>> pkts;
                if (DecodePackets(buffer, pkts) && !pkts.empty()) {
                    client_conn2->OnPackets(0, pkts);
                }
            }
        }
        // client -> server
        {
            auto buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1500));
            quic::SendOperation op;
            if (client_conn2->GenerateSendData(buffer, op)) {
                std::vector<std::shared_ptr<IPacket>> pkts;
                if (DecodePackets(buffer, pkts) && !pkts.empty()) {
                    server_conn2->OnPackets(0, pkts);
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
    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // client -------init-----> server
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    // client <------init------ server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    // client <---handshake---- server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    // client ----handshake---> server
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    // client <----session----- server
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));

    EXPECT_EQ(server_conn->GetCurEncryptionLevel(), kApplication);
    EXPECT_EQ(client_conn->GetCurEncryptionLevel(), kApplication);

    std::string session_der;
    ASSERT_TRUE(client_conn->ExportResumptionSession(session_der));
    ASSERT_FALSE(session_der.empty());
    common::LOG_DEBUG("session_der: %s, size: %zu", session_der.c_str(), session_der.size());

    // 2) Second connection: provide session to enable 0-RTT and send early data
    auto client_conn2 = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    client_conn2->Dial(addr, "h3", session_der, DEFAULT_QUIC_TRANSPORT_PARAMS);


    std::shared_ptr<TLSServerCtx> server_ctx_2 = std::make_shared<TLSServerCtx>();
    server_ctx_2->Init(kCertPem, kKeyPem, false, 172800);

    auto server_conn2 = std::make_shared<ServerConnection>(server_ctx_2, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn2->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // queue early application data before handshake completes
    auto s_base = client_conn2->MakeStream(StreamDirection::kSend);
    auto s = std::dynamic_pointer_cast<IQuicSendStream>(s_base);
    ASSERT_NE(s, nullptr);
    const char* early = "hello 0rtt";
    ASSERT_GT(s->Send((uint8_t*)early, (uint32_t)strlen(early)), 0);

    // First flight from client should be Initial or 0-RTT (if session supports it)
    std::shared_ptr<common::SingleBlockBuffer> buffer1 = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1500));
    quic::SendOperation op1;
    ASSERT_TRUE(client_conn2->GenerateSendData(buffer1, op1));
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
        auto buffern = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(1500));
        quic::SendOperation opn;
        if (!client_conn2->GenerateSendData(buffern, opn)) {
            // If no more data to send, break
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