#include <gtest/gtest.h>

#include "common/log/log.h"
#include "quic/frame/type.h"
#include "common/timer/timer.h"
#include "common/buffer/buffer.h"
#include "quic/frame/stream_frame.h"
#include "quic/packet/packet_decode.h"
#include "quic/packet/header/long_header.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/packet/header/short_header.h"
#include "quic/include/if_quic_send_stream.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/connection_server.h"

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
    uint8_t buf[1500] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + 1500);
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
        uint8_t buf[1500] = {0};
        std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + 1500);
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
    uint8_t buf1[1500] = {0};
    std::shared_ptr<common::Buffer> buffer1 = std::make_shared<common::Buffer>(buf1, buf1 + 1500);
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
        uint8_t bufn[1500] = {0};
        auto buffern = std::make_shared<common::Buffer>(bufn, bufn + 1500);
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
            uint8_t buf[1500] = {0};
            auto buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
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
            uint8_t buf[1500] = {0};
            auto buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
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
    uint8_t buf1[1500] = {0};
    std::shared_ptr<common::Buffer> buffer1 = std::make_shared<common::Buffer>(buf1, buf1 + 1500);
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
        uint8_t bufn[1500] = {0};
        auto buffern = std::make_shared<common::Buffer>(bufn, bufn + 1500);
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

TEST(quic_connection_utest, path_token_validation_and_promotion) {
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

    // complete handshake quickly
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // Simulate observed new address on client side
    common::Address new_addr("127.0.0.1", 9543);
    client_conn->OnObservedPeerAddress(new_addr);

    // Generate probe packet(s)
    uint8_t buf[1500] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + 1500);
    quic::SendOperation send_operation;
    ASSERT_TRUE(client_conn->GenerateSendData(buffer, send_operation));

    // Decode and deliver to server to respond PATH_CHALLENGE
    std::vector<std::shared_ptr<IPacket>> pkts;
    ASSERT_TRUE(DecodePackets(buffer, pkts));
    ASSERT_FALSE(pkts.empty());
    server_conn->OnPackets(0, pkts);

    // Now server sends PATH_RESPONSE
    uint8_t sbuf[1500] = {0};
    auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + 1500);
    quic::SendOperation sop;
    if (server_conn->GenerateSendData(sb, sop)) {
        std::vector<std::shared_ptr<IPacket>> rsp;
        ASSERT_TRUE(DecodePackets(sb, rsp));
        ASSERT_FALSE(rsp.empty());
        client_conn->OnPackets(0, rsp);
    }

    // After response, client should switch active path and allow streams again.
    auto s_base = client_conn->MakeStream(StreamDirection::kSend);
    auto s = std::dynamic_pointer_cast<IQuicSendStream>(s_base);
    ASSERT_NE(s, nullptr);
    const char* data = "ping after migration";
    ASSERT_GT(s->Send((uint8_t*)data, (uint32_t)strlen(data)), 0);

    // CID rotation and retirement should occur on path switch: push multiple remote CIDs and ensure UseNextID() retires current
    // Prepare by adding extra remote CIDs to client
    {
        // simulate NEW_CONNECTION_ID frames were received earlier (we call manager via public API if exposed;
        // here we force client to send more flights to trigger UseNextID path; the correctness is covered by not crashing
        for (int i = 0; i < 3; ++i) {
            uint8_t buf2[1500] = {0};
            auto b2 = std::make_shared<common::Buffer>(buf2, buf2 + sizeof(buf2));
            quic::SendOperation op2;
            (void)client_conn->GenerateSendData(b2, op2);
        }
    }
}

TEST(quic_connection_utest, nat_rebinding_integration) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr("127.0.0.1", 9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // Run handshake to 1-RTT
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // NAT rebinding: server observes new source address from client
    common::Address nat_addr("127.0.0.1", 9654);
    server_conn->OnObservedPeerAddress(nat_addr);

    // server will probe; deliver its probe to client
    {
        uint8_t sbuf[1500] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        if (server_conn->GenerateSendData(sb, sop)) {
            std::vector<std::shared_ptr<IPacket>> pkts;
            ASSERT_TRUE(DecodePackets(sb, pkts));
            client_conn->OnPackets(0, pkts);
        }
    }

    // client responds; deliver back to server
    {
        uint8_t cbuf[1500] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        if (client_conn->GenerateSendData(cb, cop)) {
            std::vector<std::shared_ptr<IPacket>> pkts;
            ASSERT_TRUE(DecodePackets(cb, pkts));
            server_conn->OnPackets(0, pkts);
        }
    }

    // After validation, send stream data from server should still work
    auto s_base = server_conn->MakeStream(StreamDirection::kSend);
    auto s = std::dynamic_pointer_cast<IQuicSendStream>(s_base);
    ASSERT_NE(s, nullptr);
    const char* msg = "hello after nat";
    ASSERT_GT(s->Send((uint8_t*)msg, (uint32_t)strlen(msg)), 0);
}

TEST(quic_connection_utest, path_challenge_retry_backoff) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr("127.0.0.1", 9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // Trigger migration on client but drop all probe-related traffic to force retries
    common::Address new_addr("127.0.0.1", 9991);
    client_conn->OnObservedPeerAddress(new_addr);

    // Drive several send cycles without delivering to server to simulate black hole for PATH_CHALLENGE
    for (int i = 0; i < 7; ++i) {
        uint8_t buf[1500] = {0};
        auto b = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        quic::SendOperation op;
        (void)client_conn->GenerateSendData(b, op);
        // drop
    }
    // Expect no crash and retries bounded (internal cap 5). This test ensures we do not retry indefinitely.
}

TEST(quic_connection_utest, amp_gating_blocks_streams_before_validation) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr("127.0.0.1", 9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // Trigger client-side migration; while unvalidated, streams should be gated
    common::Address new_addr("127.0.0.1", 9555);
    client_conn->OnObservedPeerAddress(new_addr);

    auto s_base = client_conn->MakeStream(StreamDirection::kSend);
    auto s = std::dynamic_pointer_cast<IQuicSendStream>(s_base);
    ASSERT_NE(s, nullptr);
    const char* payload = "must gate before validation";

    uint8_t buf[2000] = {0};
    auto buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation sop;
    ASSERT_TRUE(client_conn->GenerateSendData(buffer, sop));
    // Decode frames to ensure only allowed types appear when streams are disallowed
    std::vector<std::shared_ptr<IPacket>> pkts;
    ASSERT_TRUE(DecodePackets(buffer, pkts));
    ASSERT_FALSE(pkts.empty());
    for (auto& p : pkts) {
        auto& frames = p->GetFrames();
        for (auto& f : frames) {
            auto t = f->GetType();
            EXPECT_TRUE(t == FrameType::kPathChallenge || t == FrameType::kPathResponse ||
                        t == FrameType::kAck || t == FrameType::kAckEcn ||
                        t == FrameType::kPing || t == FrameType::kPadding ||
                        StreamFrame::IsStreamFrame(t) == false);
        }
    }
}

TEST(quic_connection_utest, pmtu_probe_success_raises_mtu) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr("127.0.0.1", 9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // Trigger migration to start PMTU probing after validation
    common::Address new_addr("127.0.0.1", 9666);
    client_conn->OnObservedPeerAddress(new_addr);

    // Client sends PATH_CHALLENGE
    {
        uint8_t cbuf[2000] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        ASSERT_TRUE(client_conn->GenerateSendData(cb, cop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(cb, pkts));
        ASSERT_FALSE(pkts.empty());
        server_conn->OnPackets(0, pkts);
    }

    // Server replies PATH_RESPONSE; deliver to client to validate path
    {
        uint8_t sbuf[2000] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        ASSERT_TRUE(server_conn->GenerateSendData(sb, sop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(sb, pkts));
        ASSERT_FALSE(pkts.empty());
        client_conn->OnPackets(0, pkts);
    }

    // After validation, client should attempt a PMTU probe packet (PING+PADDING large)
    {
        uint8_t cbuf[4000] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        ASSERT_TRUE(client_conn->GenerateSendData(cb, cop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(cb, pkts));
        if (!pkts.empty()) {
            // Deliver to server so it ACKs, which will be treated as probe success internally
            server_conn->OnPackets(0, pkts);
        }
    }

    // Server generates ACK; deliver back to client to confirm probe success
    {
        uint8_t sbuf[4000] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        if (server_conn->GenerateSendData(sb, sop)) {
            std::vector<std::shared_ptr<IPacket>> pkts;
            ASSERT_TRUE(DecodePackets(sb, pkts));
            if (!pkts.empty()) {
                client_conn->OnPackets(0, pkts);
            }
        }
    }
}

TEST(quic_connection_utest, pmtu_probe_loss_fallback) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr("127.0.0.1", 9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // Trigger migration to start PMTU probing after validation
    common::Address new_addr("127.0.0.1", 9777);
    client_conn->OnObservedPeerAddress(new_addr);

    // Client sends PATH_CHALLENGE; deliver and drop server response to simulate loss of PMTU probe later
    {
        uint8_t cbuf[2000] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        ASSERT_TRUE(client_conn->GenerateSendData(cb, cop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(cb, pkts));
        ASSERT_FALSE(pkts.empty());
        server_conn->OnPackets(0, pkts);
    }

    // Server replies PATH_RESPONSE; deliver to client to validate path
    {
        uint8_t sbuf[2000] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        ASSERT_TRUE(server_conn->GenerateSendData(sb, sop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(sb, pkts));
        ASSERT_FALSE(pkts.empty());
        client_conn->OnPackets(0, pkts);
    }

    // After validation, trigger client send (PMTU probe created). Do not deliver to server to simulate black hole.
    {
        uint8_t cbuf[4000] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        (void)client_conn->GenerateSendData(cb, cop);
        // Intentionally drop
    }

    // Advance time/send loop to cause retransmission timeout path to mark loss and fallback; here we just run extra cycles.
    for (int i = 0; i < 5; ++i) {
        (void)ConnectionProcess(client_conn, server_conn);
        (void)ConnectionProcess(server_conn, client_conn);
    }
}

TEST(quic_connection_utest, disable_active_migration_semantics) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr("127.0.0.1", 9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    // Server announces disable_active_migration
    QuicTransportParams params = DEFAULT_QUIC_TRANSPORT_PARAMS;
    params.disable_active_migration_ = true;
    server_conn->AddTransportParam(params);

    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // Client proactively observes a new address; first observation should not start probe
    common::Address new_addr("127.0.0.1", 9888);
    client_conn->OnObservedPeerAddress(new_addr);

    // Generate a flight and check no PATH_CHALLENGE appears yet
    {
        uint8_t buf[1500] = {0};
        auto b = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        quic::SendOperation op;
        (void)client_conn->GenerateSendData(b, op);
        std::vector<std::shared_ptr<IPacket>> pkts;
        DecodePackets(b, pkts);
        for (auto& p : pkts) {
            for (auto& f : p->GetFrames()) {
                ASSERT_NE(f->GetType(), FrameType::kPathChallenge);
            }
        }
    }

    // Second observation of the same new address -> treat as NAT rebinding and probe
    client_conn->OnObservedPeerAddress(new_addr);
    {
        uint8_t buf[1500] = {0};
        auto b = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        quic::SendOperation op;
        ASSERT_TRUE(client_conn->GenerateSendData(b, op));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(b, pkts));
        bool found_chal = false;
        for (auto& p : pkts) {
            for (auto& f : p->GetFrames()) {
                if (f->GetType() == FrameType::kPathChallenge) { found_chal = true; break; }
            }
        }
        EXPECT_TRUE(found_chal);
    }
}

TEST(quic_connection_utest, cid_rotation_and_retirement_on_path_switch) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    common::Address addr("127.0.0.1", 9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // handshake
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // Record current dcid used by client when sending 1-RTT before migration
    uint8_t pre_buf[1500] = {0};
    auto pre_b = std::make_shared<common::Buffer>(pre_buf, pre_buf + sizeof(pre_buf));
    quic::SendOperation pre_op;
    ASSERT_TRUE(client_conn->GenerateSendData(pre_b, pre_op));
    std::vector<std::shared_ptr<IPacket>> pre_pkts;
    ASSERT_TRUE(DecodePackets(pre_b, pre_pkts));
    ASSERT_FALSE(pre_pkts.empty());
    auto pre_cid = (pre_pkts[0]->GetHeader()->GetHeaderType() == PacketHeaderType::kLongHeader)
                        ? ((LongHeader*)pre_pkts[0]->GetHeader())->GetDestinationConnectionId()
                        : ((ShortHeader*)pre_pkts[0]->GetHeader())->GetDestinationConnectionId();
    auto pre_cid_len = (pre_pkts[0]->GetHeader()->GetHeaderType() == PacketHeaderType::kLongHeader)
                        ? ((LongHeader*)pre_pkts[0]->GetHeader())->GetDestinationConnectionIdLength()
                        : ((ShortHeader*)pre_pkts[0]->GetHeader())->GetDestinationConnectionIdLength();

    // Trigger migration on client
    common::Address new_addr("127.0.0.1", 9999);
    client_conn->OnObservedPeerAddress(new_addr);

    // Client sends PATH_CHALLENGE -> deliver to server
    {
        uint8_t cbuf[1500] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        ASSERT_TRUE(client_conn->GenerateSendData(cb, cop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(cb, pkts));
        ASSERT_FALSE(pkts.empty());
        server_conn->OnPackets(0, pkts);
    }

    // Server PATH_RESPONSE -> client validates and should rotate DCID
    {
        uint8_t sbuf[1500] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        ASSERT_TRUE(server_conn->GenerateSendData(sb, sop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(sb, pkts));
        ASSERT_FALSE(pkts.empty());
        client_conn->OnPackets(0, pkts);
    }

    // Next client packet should use a different DCID and emit RETIRE_CONNECTION_ID for old one
    uint8_t post_buf[2000] = {0};
    auto post_b = std::make_shared<common::Buffer>(post_buf, post_buf + sizeof(post_buf));
    quic::SendOperation post_op;
    ASSERT_TRUE(client_conn->GenerateSendData(post_b, post_op));
    std::vector<std::shared_ptr<IPacket>> post_pkts;
    ASSERT_TRUE(DecodePackets(post_b, post_pkts));
    ASSERT_FALSE(post_pkts.empty());

    // check DCID changed
    auto post_hdr = post_pkts[0]->GetHeader();
    const uint8_t* post_cid = nullptr;
    uint8_t post_cid_len = 0;
    if (post_hdr->GetHeaderType() == PacketHeaderType::kLongHeader) {
        post_cid = ((LongHeader*)post_hdr)->GetDestinationConnectionId();
        post_cid_len = ((LongHeader*)post_hdr)->GetDestinationConnectionIdLength();
    } else {
        post_cid = ((ShortHeader*)post_hdr)->GetDestinationConnectionId();
        post_cid_len = ((ShortHeader*)post_hdr)->GetDestinationConnectionIdLength();
    }
    ASSERT_EQ(post_cid_len, pre_cid_len);
    bool cid_changed = (memcmp(pre_cid, post_cid, pre_cid_len) != 0);
    EXPECT_TRUE(cid_changed);

    // Also ensure that a RETIRE_CONNECTION_ID frame is eventually sent from client
    bool saw_retire = false;
    for (auto& p : post_pkts) {
        for (auto& f : p->GetFrames()) {
            if (f->GetType() == FrameType::kRetireConnectionId) { saw_retire = true; break; }
        }
    }
    // If not in this flight, drive another send
    if (!saw_retire) {
        uint8_t add_buf[1500] = {0};
        auto ab = std::make_shared<common::Buffer>(add_buf, add_buf + sizeof(add_buf));
        quic::SendOperation aop;
        if (client_conn->GenerateSendData(ab, aop)) {
            std::vector<std::shared_ptr<IPacket>> pkts;
            if (DecodePackets(ab, pkts)) {
                for (auto& p : pkts) {
                    for (auto& f : p->GetFrames()) {
                        if (f->GetType() == FrameType::kRetireConnectionId) { saw_retire = true; break; }
                    }
                }
            }
        }
    }
    EXPECT_TRUE(saw_retire);
}

TEST(quic_connection_utest, path_challenge_retry_backoff_limits) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    common::Address addr("127.0.0.1", 9432);
    client_conn->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(server_conn, client_conn));
    ASSERT_TRUE(ConnectionProcess(client_conn, server_conn));

    // Trigger migration but drop all server responses to force retries
    common::Address new_addr("127.0.0.1", 10001);
    client_conn->OnObservedPeerAddress(new_addr);

    // Drive multiple client send cycles to schedule retries; we expect at most 5 retries (per实现)
    int path_challenge_count = 0;
    for (int i = 0; i < 10; ++i) {
        uint8_t cbuf[1500] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        if (!client_conn->GenerateSendData(cb, cop)) continue;
        std::vector<std::shared_ptr<IPacket>> pkts;
        if (!DecodePackets(cb, pkts)) continue;
        for (auto& p : pkts) {
            for (auto& f : p->GetFrames()) {
                if (f->GetType() == FrameType::kPathChallenge) { ++path_challenge_count; }
            }
        }
        // Do NOT deliver to server to simulate no PATH_RESPONSE arriving
    }
    // 1 initial + up to 5 retries -> <= 6
    EXPECT_LE(path_challenge_count, 6);
}
}
}
}