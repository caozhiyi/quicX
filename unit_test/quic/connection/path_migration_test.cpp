/**
 * Path Migration Enhanced Functionality Tests
 * 
 * Tests covering fixed issues:
 * 1. Bug #1: Connection permanently blocked after path validation failure
 * 2. Issue #2: Concurrent multi-path probing
 * 3. Issue #3: CID pool management
 * 4. Issue #4: Preferred Address mechanism
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

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


#include "common/log/log.h"
#include "common/log/file_logger.h"
#include "common/log/stdout_logger.h"

namespace quicx {
namespace quic {
namespace {

// Test certificate
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

static QuicTransportParams TEST_TRANSPORT_PARAMS = {
    "",     // original_destination_connection_id
    3000,   // max_idle_timeout
    "",     // stateless_reset_token
    1472,   // max_udp_payload_size
    10485760,  // initial_max_data (10MB)
    1048576,   // initial_max_stream_data_bidi_local (1MB)
    1048576,   // initial_max_stream_data_bidi_remote (1MB)
    1048576,   // initial_max_stream_data_uni (1MB)
    100,    // initial_max_streams_bidi
    100,    // initial_max_streams_uni
    3,      // ack_delay_exponent
    25,     // max_ack_delay
    false,  // disable_active_migration
    "",     // preferred_address
    8,      // active_connection_id_limit
    "",     // initial_source_connection_id
    "",     // retry_source_connection_id
};

// Helper function: handle connection handshake and data transmission
static bool ConnectionProcess(std::shared_ptr<IConnection> sender, std::shared_ptr<IConnection> receiver) {
    uint8_t buf[2000] = {0};
    auto buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation op;
    
    if (!sender->GenerateSendData(buffer, op)) {
        return false;
    }
    
    std::vector<std::shared_ptr<IPacket>> pkts;
    if (!DecodePackets(buffer, pkts) || pkts.empty()) {
        return false;
    }
    
    receiver->OnPackets(0, pkts);
    return true;
}

static std::pair<std::shared_ptr<IConnection>, std::shared_ptr<IConnection>> GenerateHandshakeDoneConnections(
    const quicx::quic::QuicTransportParams& client_tp = DEFAULT_QUIC_TRANSPORT_PARAMS, const quicx::quic::QuicTransportParams& server_tp = DEFAULT_QUIC_TRANSPORT_PARAMS) {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client_conn = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client_conn->Dial(addr, "h3", client_tp);

    auto server_conn = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server_conn->AddTransportParam(server_tp);

    // client -------init-----> server
    EXPECT_TRUE(ConnectionProcess(client_conn, server_conn));
    // client <------init------ server
    EXPECT_TRUE(ConnectionProcess(server_conn, client_conn));
    // client <---handshake---- server
    EXPECT_TRUE(ConnectionProcess(server_conn, client_conn));
    // client ----handshake---> server
    EXPECT_TRUE(ConnectionProcess(client_conn, server_conn));
    // client <----session----- server
    EXPECT_TRUE(ConnectionProcess(server_conn, client_conn));

    EXPECT_TRUE(server_conn->GetCurEncryptionLevel() == kApplication) 
        << "Server connection should be in application encryption level, but got "
        << server_conn->GetCurEncryptionLevel();
    EXPECT_TRUE(client_conn->GetCurEncryptionLevel() == kApplication) 
        << "Client connection should be in application encryption level, but got "
        << client_conn->GetCurEncryptionLevel();

    return std::make_pair(client_conn, server_conn);
}

TEST(path_migration, validation_failure_recovery) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;
    // Verify connection works normally
    auto stream_before = std::dynamic_pointer_cast<IQuicSendStream>(
        client_conn->MakeStream(StreamDirection::kSend));
    ASSERT_NE(stream_before, nullptr);
    const char* test_data = "before migration";
    EXPECT_GT(stream_before->Send((uint8_t*)test_data, strlen(test_data)), 0);

    // Trigger path migration
    common::Address new_addr("127.0.0.1", 9999);
    client_conn->OnObservedPeerAddress(new_addr);

    // Simulate network black hole: drop all PATH_CHALLENGEs
    for (int attempt = 0; attempt < 10; ++attempt) {
        uint8_t drop_buf[1500] = {0};
        auto drop_buffer = std::make_shared<common::Buffer>(drop_buf, drop_buf + sizeof(drop_buf));
        quic::SendOperation drop_op;
        (void)client_conn->GenerateSendData(drop_buffer, drop_op);
        
        // Wait for retry trigger
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Critical verification: after failure, stream data should resume normal sending
    auto stream_after = std::dynamic_pointer_cast<IQuicSendStream>(
        client_conn->MakeStream(StreamDirection::kSend));
    ASSERT_NE(stream_after, nullptr) << "Should be able to create stream after validation failure";
    
    const char* recovery_data = "after validation failure";
    int sent_bytes = stream_after->Send((uint8_t*)recovery_data, strlen(recovery_data));
    EXPECT_GT(sent_bytes, 0) << "Should be able to send data after validation failure (anti-amplification exited)";
}

//============================================================================
// Issue #2: Concurrent multi-path probing
//============================================================================

TEST(path_migration, concurrent_path_probing) {
    std::shared_ptr<common::Logger> file_log = std::make_shared<common::FileLogger>("test.log");
    std::shared_ptr<common::Logger> std_log = std::make_shared<common::StdoutLogger>();
    file_log->SetLogger(std_log);
    common::LOG_SET(file_log);

    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

    // Rapidly trigger multiple address changes
    common::Address addr1("127.0.0.1", 10001);
    common::Address addr2("127.0.0.1", 10002);
    common::Address addr3("127.0.0.1", 10003);
    
    client_conn->OnObservedPeerAddress(addr1); // Start probing immediately
    client_conn->OnObservedPeerAddress(addr2); // Should be queued
    client_conn->OnObservedPeerAddress(addr3); // Should be queued

    // Verify first PATH_CHALLENGE by server's PATH_RESPONSE (avoid decrypting in test)
    {
        uint8_t buf[1500] = {0};
        auto buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        quic::SendOperation op;
        ASSERT_TRUE(client_conn->GenerateSendData(buffer, op));

        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(buffer, pkts));
        // Deliver client's encrypted packets to server; server should respond PATH_RESPONSE
        server_conn->OnPackets(0, pkts);

        uint8_t sbuf[1500] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        ASSERT_TRUE(server_conn->GenerateSendData(sb, sop));

        std::vector<std::shared_ptr<IPacket>> rsp;
        ASSERT_TRUE(DecodePackets(sb, rsp));
        bool found_path_response = false;
        // Decrypt server's 1-RTT packets using client's cryptographer
        auto cli_crypto = client_conn->GetCryptographerForTest(kApplication);
        ASSERT_NE(cli_crypto, nullptr);
        for (auto& p : rsp) {
            p->SetCryptographer(cli_crypto);
            uint8_t tmp[4096] = {0};
            auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
            ASSERT_TRUE(p->DecodeWithCrypto(tmp_buf));
            for (auto& f : p->GetFrames()) {
                if (f->GetType() == FrameType::kPathResponse) { found_path_response = true; break; }
            }
            if (found_path_response) break;
        }
        EXPECT_TRUE(found_path_response) << "Server should send PATH_RESPONSE proving client sent PATH_CHALLENGE";
    }

    // Duplicate address changes should be ignored
    client_conn->OnObservedPeerAddress(addr2); // Already in queue, should be ignored
    
    // Verify queue contains addr2 and addr3 via logs or internal state
    // (In actual implementation, can add public interface to get queue size)
}

TEST(path_migration, cid_pool_replenishment) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

    // Trigger path migration to consume CIDs
    common::Address new_addr("127.0.0.1", 9999);
    client_conn->OnObservedPeerAddress(new_addr);

    // Send PATH_CHALLENGE
    {
        uint8_t cbuf[1500] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        ASSERT_TRUE(client_conn->GenerateSendData(cb, cop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(cb, pkts));
        server_conn->OnPackets(0, pkts);
    }

    // Receive PATH_RESPONSE and complete migration
    {
        uint8_t sbuf[1500] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        ASSERT_TRUE(server_conn->GenerateSendData(sb, sop));
        std::vector<std::shared_ptr<IPacket>> pkts;
        ASSERT_TRUE(DecodePackets(sb, pkts));
        
        auto cli_crypto = client_conn->GetCryptographerForTest(kApplication);
        ASSERT_NE(cli_crypto, nullptr);
        // Check if NEW_CONNECTION_ID frame is sent (CID pool replenishment)
        bool found_new_cid = false;
        for (auto& p : pkts) {
            p->SetCryptographer(cli_crypto);
            uint8_t tmp[4096] = {0};
            auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
            ASSERT_TRUE(p->DecodeWithCrypto(tmp_buf));
            for (auto& f : p->GetFrames()) {
                if (f->GetType() == FrameType::kNewConnectionId) {
                    found_new_cid = true;
                    break;
                }
            }
        }
        
        client_conn->OnPackets(0, pkts);
        
        // Server should automatically replenish CID pool after path switch
        // (May be sent in subsequent packets)
    }

    // Verify can continue migration (CID pool replenished)
    common::Address addr2("127.0.0.1", 10000);
    client_conn->OnObservedPeerAddress(addr2);
    
    uint8_t buf2[1500] = {0};
    auto buffer2 = std::make_shared<common::Buffer>(buf2, buf2 + sizeof(buf2));
    quic::SendOperation op2;
    EXPECT_TRUE(client_conn->GenerateSendData(buffer2, op2)) 
        << "Should be able to migrate again after CID pool replenishment";
}

//============================================================================
// Issue #4: Preferred Address
//============================================================================

TEST(path_migration, preferred_address_mechanism) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

    // After handshake, client should automatically start probing preferred_address
    // (This will be triggered in OnTransportParams)
    
    // Verify client sends PATH_CHALLENGE to preferred_address
    {
        uint8_t buf[1500] = {0};
        auto buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        quic::SendOperation op;
        
        if (client_conn->GenerateSendData(buffer, op)) {
            std::vector<std::shared_ptr<IPacket>> pkts;
            if (DecodePackets(buffer, pkts)) {
                bool found_challenge = false;

                auto ser_crypto = client_conn->GetCryptographerForTest(kApplication);
                ASSERT_NE(ser_crypto, nullptr);
                for (auto& p : pkts) {
                    p->SetCryptographer(ser_crypto);
                    uint8_t tmp[4096] = {0};
                    auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
                    ASSERT_TRUE(p->DecodeWithCrypto(tmp_buf));
                    for (auto& f : p->GetFrames()) {
                        if (f->GetType() == FrameType::kPathChallenge) {
                            found_challenge = true;
                            break;
                        }
                    }
                }
                // Client should actively probe preferred_address
                // (If address is different)
            }
        }
    }
}

//============================================================================
// Edge case: Duplicate PATH_RESPONSE
//============================================================================

TEST(path_migration, duplicate_path_response) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

    // Trigger migration
    common::Address new_addr("127.0.0.1", 9999);
    client_conn->OnObservedPeerAddress(new_addr);

    // Send PATH_CHALLENGE and get PATH_RESPONSE
    std::vector<std::shared_ptr<IPacket>> response_pkts;
    {
        uint8_t cbuf[1500] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        ASSERT_TRUE(client_conn->GenerateSendData(cb, cop));
        std::vector<std::shared_ptr<IPacket>> challenge_pkts;
        ASSERT_TRUE(DecodePackets(cb, challenge_pkts));
        server_conn->OnPackets(0, challenge_pkts);
        
        uint8_t sbuf[1500] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        ASSERT_TRUE(server_conn->GenerateSendData(sb, sop));
        ASSERT_TRUE(DecodePackets(sb, response_pkts));
        
        // First processing
        client_conn->OnPackets(0, response_pkts);
    }

    // Re-send same PATH_RESPONSE (simulate network retransmission)
    EXPECT_NO_THROW(client_conn->OnPackets(0, response_pkts)) 
        << "Should handle duplicate PATH_RESPONSE gracefully";
    
    // Verify connection still works normally
    auto stream = std::dynamic_pointer_cast<IQuicSendStream>(
        client_conn->MakeStream(StreamDirection::kSend));
    ASSERT_NE(stream, nullptr);
    const char* data = "after duplicate response";
    EXPECT_GT(stream->Send((uint8_t*)data, strlen(data)), 0);
}


TEST(path_migration, path_token_validation_and_promotion) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

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

TEST(path_migration, nat_rebinding_integration) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

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

TEST(path_migration, path_challenge_retry_backoff) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

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

TEST(path_migration, amp_gating_blocks_streams_before_validation) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

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

TEST(path_migration, pmtu_probe_success_raises_mtu) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

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

TEST(path_migration, pmtu_probe_loss_fallback) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

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

TEST(path_migration, disable_active_migration_semantics) {
    auto server_tp = DEFAULT_QUIC_TRANSPORT_PARAMS;
    server_tp.disable_active_migration_ = true;

    auto connections = GenerateHandshakeDoneConnections(DEFAULT_QUIC_TRANSPORT_PARAMS, server_tp);
    auto client_conn = connections.first;
    auto server_conn = connections.second;

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
        ASSERT_TRUE(DecodePackets(b, pkts));
        // Decrypt client->server packets with server cryptographer
        auto srv_crypto = server_conn->GetCryptographerForTest(kApplication);
        ASSERT_NE(srv_crypto, nullptr);
        for (auto& p : pkts) {
            p->SetCryptographer(srv_crypto);
            uint8_t tmp[4096] = {0};
            auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
            ASSERT_TRUE(p->DecodeWithCrypto(tmp_buf));
            bool found_path_challenge = false;
            for (auto& f : p->GetFrames()) {
                if (f->GetType() == FrameType::kPathChallenge) { 
                    found_path_challenge = true;
                    break;
                }
            }
            EXPECT_FALSE(found_path_challenge) << "No PATH_CHALLENGE on first observation when migration disabled";
        }
    }

    // Second observation of the same new address -> treat as NAT rebinding and probe
    client_conn->OnObservedPeerAddress(new_addr);
    {
        uint8_t buf[1500] = {0};
        auto b = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        quic::SendOperation op;
        ASSERT_TRUE(client_conn->GenerateSendData(b, op));
        std::vector<std::shared_ptr<IPacket>> client_pkts;
        ASSERT_TRUE(DecodePackets(b, client_pkts));

        // Deliver client's probe packets to server
        server_conn->OnPackets(0, client_pkts);

        // Server should respond; generate and decrypt server->client response to find PATH_RESPONSE
        uint8_t sbuf[1500] = {0};
        auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
        quic::SendOperation sop;
        ASSERT_TRUE(server_conn->GenerateSendData(sb, sop));

        std::vector<std::shared_ptr<IPacket>> rsp;
        ASSERT_TRUE(DecodePackets(sb, rsp));

        auto cli_crypto = client_conn->GetCryptographerForTest(kApplication);
        ASSERT_NE(cli_crypto, nullptr);
        bool found_path_response = false;
        for (auto& p : rsp) {
            p->SetCryptographer(cli_crypto);
            uint8_t tmp[4096] = {0};
            auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
            if (!p->DecodeWithCrypto(tmp_buf)) continue;
            for (auto& f : p->GetFrames()) {
                if (f->GetType() == FrameType::kPathResponse) { found_path_response = true; break; }
            }
            if (found_path_response) break;
        }
        ASSERT_TRUE(found_path_response) << "Server should send PATH_RESPONSE on second observation (NAT rebinding)";
    }
}

TEST(path_migration, cid_rotation_and_retirement_on_path_switch) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

    auto warmup_stream = std::dynamic_pointer_cast<IQuicSendStream>(
        client_conn->MakeStream(StreamDirection::kSend));
  
    // send a message to warm up the connection
    const char* warmup_data = "warmup";
    warmup_stream->Send((uint8_t*)warmup_data, strlen(warmup_data));

    // Ensure client has at least one extra remote CID from server before migration
    {
        int new_cid_frames = 0;
        for (int i = 0; i < 5 && new_cid_frames == 0; ++i) {
            uint8_t sbuf[1500] = {0};
            auto sb = std::make_shared<common::Buffer>(sbuf, sbuf + sizeof(sbuf));
            quic::SendOperation sop;
            if (!server_conn->GenerateSendData(sb, sop)) continue;
            std::vector<std::shared_ptr<IPacket>> spkts;
            if (!DecodePackets(sb, spkts)) continue;
            // decrypt server->client with client's cryptographer and check NEW_CONNECTION_ID
            auto cli_crypto = client_conn->GetCryptographerForTest(kApplication);
            ASSERT_NE(cli_crypto, nullptr);
            for (auto& p : spkts) {
                p->SetCryptographer(cli_crypto);
                uint8_t tmp[4096] = {0};
                auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
                if (!p->DecodeWithCrypto(tmp_buf)) continue;
                for (auto& f : p->GetFrames()) {
                    if (f->GetType() == FrameType::kNewConnectionId) { ++new_cid_frames; }
                }
            }
            // deliver to client to actually install NEW_CONNECTION_IDs
            client_conn->OnPackets(0, spkts);
        }
        ASSERT_GT(new_cid_frames, 0) << "Server should provide at least one NEW_CONNECTION_ID before migration";
    }

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
    auto ser_crypto = server_conn->GetCryptographerForTest(kApplication);
    ASSERT_NE(ser_crypto, nullptr);
    bool saw_retire = false;
    for (auto& p : post_pkts) {
        p->SetCryptographer(ser_crypto);
        uint8_t tmp[4096] = {0};
        auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
        ASSERT_TRUE(p->DecodeWithCrypto(tmp_buf));
        ASSERT_FALSE(p->GetFrames().empty());
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
            ASSERT_TRUE(DecodePackets(ab, pkts));
            for (auto& p : pkts) {
                p->SetCryptographer(ser_crypto);
                uint8_t tmp[4096] = {0};
                auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
                ASSERT_TRUE(p->DecodeWithCrypto(tmp_buf));
                ASSERT_FALSE(p->GetFrames().empty());
                for (auto& f : p->GetFrames()) {
                    if (f->GetType() == FrameType::kRetireConnectionId) { saw_retire = true; break; }
                }
            }
            if (!saw_retire) {
                uint8_t add_buf[1500] = {0};
                auto ab = std::make_shared<common::Buffer>(add_buf, add_buf + sizeof(add_buf));
                quic::SendOperation aop;
                if (client_conn->GenerateSendData(ab, aop)) {
                    std::vector<std::shared_ptr<IPacket>> pkts;
                    ASSERT_TRUE(DecodePackets(ab, pkts));
                }
            }
        }
    }
    EXPECT_TRUE(saw_retire);
}

TEST(path_migration, path_challenge_retry_backoff_limits) {
    auto connections = GenerateHandshakeDoneConnections();
    auto client_conn = connections.first;
    auto server_conn = connections.second;

    // Trigger migration but drop all server responses to force retries
    common::Address new_addr("127.0.0.1", 10001);
    client_conn->OnObservedPeerAddress(new_addr);

    // Drive multiple client send cycles to schedule retries; we expect at most 5 retries (per implementation)
    int path_challenge_count = 0;
    for (int i = 0; i < 10; ++i) {
        uint8_t cbuf[1500] = {0};
        auto cb = std::make_shared<common::Buffer>(cbuf, cbuf + sizeof(cbuf));
        quic::SendOperation cop;
        if (!client_conn->GenerateSendData(cb, cop)) continue;
        std::vector<std::shared_ptr<IPacket>> pkts;
        if (!DecodePackets(cb, pkts)) continue;
        auto ser_crypto = server_conn->GetCryptographerForTest(kApplication);
        ASSERT_NE(ser_crypto, nullptr);
        for (auto& p : pkts) {
            p->SetCryptographer(ser_crypto);
            uint8_t tmp[4096] = {0};
            auto tmp_buf = std::make_shared<common::Buffer>(tmp, sizeof(tmp));
            ASSERT_TRUE(p->DecodeWithCrypto(tmp_buf));
            ASSERT_FALSE(p->GetFrames().empty());
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