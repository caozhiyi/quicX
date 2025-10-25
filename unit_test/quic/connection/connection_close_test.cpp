#include <gtest/gtest.h>
#include <memory>

#include "quic/frame/type.h"
#include "common/timer/timer.h"
#include "common/buffer/buffer.h"
#include "quic/packet/packet_decode.h"
#include "quic/crypto/tls/tls_ctx_client.h"
#include "quic/crypto/tls/tls_ctx_server.h"
#include "quic/connection/connection_base.h"
#include "quic/include/if_quic_send_stream.h"
#include "quic/connection/connection_client.h"
#include "quic/connection/connection_server.h"
#include "quic/frame/connection_close_frame.h"

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

// Helper function to exchange packets between two connections
static bool ExchangePackets(std::shared_ptr<IConnection> sender, std::shared_ptr<IConnection> receiver) {
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    
    if (!sender->GenerateSendData(buffer, send_operation)) {
        return false;
    }
    
    if (buffer->GetDataLength() == 0) {
        return false;
    }

    std::vector<std::shared_ptr<IPacket>> packets;
    if (!DecodePackets(buffer, packets) || packets.empty()) {
        return false;
    }

    receiver->OnPackets(0, packets);
    return true;
}

// Helper function to establish a connection
static std::pair<std::shared_ptr<IConnection>, std::shared_ptr<IConnection>> EstablishConnection() {
    std::shared_ptr<TLSServerCtx> server_ctx = std::make_shared<TLSServerCtx>();
    server_ctx->Init(kCertPem, kKeyPem, true, 172800);

    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);

    auto server = std::make_shared<ServerConnection>(server_ctx, "h3", common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);
    server->AddTransportParam(DEFAULT_QUIC_TRANSPORT_PARAMS);

    // Complete handshake
    EXPECT_TRUE(ExchangePackets(client, server));   // client init -> server
    EXPECT_TRUE(ExchangePackets(server, client));   // server init -> client
    EXPECT_TRUE(ExchangePackets(server, client));   // server handshake -> client
    EXPECT_TRUE(ExchangePackets(client, server));   // client handshake -> server
    EXPECT_TRUE(ExchangePackets(server, client));   // server session -> client

    EXPECT_EQ(server->GetCurEncryptionLevel(), kApplication);
    EXPECT_EQ(client->GetCurEncryptionLevel(), kApplication);

    return std::make_pair(client, server);
}

// Test fixture for connection close tests
class ConnectionCloseTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

// Test 1: Graceful close with no pending data
TEST_F(ConnectionCloseTest, GracefulCloseNoPendingData) {
    auto connections = EstablishConnection();
    auto client = connections.first;
    auto server = connections.second;
    
    // Cast to BaseConnection to access test interface
    auto client_base = std::dynamic_pointer_cast<BaseConnection>(client);
    auto server_base = std::dynamic_pointer_cast<BaseConnection>(server);
    ASSERT_NE(client_base, nullptr);
    ASSERT_NE(server_base, nullptr);
    
    // Verify initial state
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    
    // Close the client connection gracefully
    client->Close();
    
    // Verify client enters Closing state
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    
    // Client should send some data (CONNECTION_CLOSE)
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    EXPECT_TRUE(client->GenerateSendData(buffer, send_operation));
    EXPECT_GT(buffer->GetDataLength(), 0);
    
    std::vector<std::shared_ptr<IPacket>> packets;
    ASSERT_TRUE(DecodePackets(buffer, packets));
    
    // Server receives the close notification
    server->OnPackets(0, packets);
    
    // Verify server enters Draining state
    EXPECT_EQ(server_base->GetConnectionStateForTest(), ConnectionStateType::kStateDraining);
    
    // Server should not send any packets in Draining state
    buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    server->GenerateSendData(buffer, send_operation);
    EXPECT_EQ(buffer->GetDataLength(), 0);
}

// Test 2: Immediate close with error
TEST_F(ConnectionCloseTest, ImmediateCloseWithError) {
    auto connections = EstablishConnection();
    auto client = connections.first;
    auto server = connections.second;
    
    auto client_base = std::dynamic_pointer_cast<BaseConnection>(client);
    ASSERT_NE(client_base, nullptr);
    
    // Verify initial state
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    
    // Create a stream with data
    auto stream = std::dynamic_pointer_cast<IQuicSendStream>(client->MakeStream(StreamDirection::kSend));
    ASSERT_NE(stream, nullptr);
    
    const char* test_data = "This data will be discarded";
    stream->Send((uint8_t*)test_data, strlen(test_data));

    // Immediately close with error using Reset
    const uint64_t error_code = 0x100;
    client->Reset(error_code);
    
    // Verify client enters Closing state immediately (no graceful wait)
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    
    // Verify CONNECTION_CLOSE is sent
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    EXPECT_TRUE(client->GenerateSendData(buffer, send_operation));
    EXPECT_GT(buffer->GetDataLength(), 0);
}

// Test 3: Peer sends CONNECTION_CLOSE, local enters Draining
TEST_F(ConnectionCloseTest, PeerInitiatedClose) {
    auto connections = EstablishConnection();
    auto client = connections.first;
    auto server = connections.second;
    
    auto client_base = std::dynamic_pointer_cast<BaseConnection>(client);
    auto server_base = std::dynamic_pointer_cast<BaseConnection>(server);
    ASSERT_NE(client_base, nullptr);
    ASSERT_NE(server_base, nullptr);
    
    // Verify initial state
    EXPECT_EQ(server_base->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    
    // Client sends CONNECTION_CLOSE
    client->Close();
    
    // Verify client enters Closing state
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    EXPECT_TRUE(client->GenerateSendData(buffer, send_operation));
    
    ASSERT_GT(buffer->GetDataLength(), 0);
    
    std::vector<std::shared_ptr<IPacket>> packets;
    ASSERT_TRUE(DecodePackets(buffer, packets));
    
    // Server receives CONNECTION_CLOSE and enters Draining
    server->OnPackets(0, packets);
    
    // Verify server enters Draining state (RFC 9000: peer-initiated close)
    EXPECT_EQ(server_base->GetConnectionStateForTest(), ConnectionStateType::kStateDraining);
    
    // Server should NOT send any packets in Draining state
    buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    server->GenerateSendData(buffer, send_operation);
    EXPECT_EQ(buffer->GetDataLength(), 0);
    
    // Even if we try to send data, it should be blocked
    auto stream = server->MakeStream(StreamDirection::kSend);
    if (stream) {
        auto send_stream = std::dynamic_pointer_cast<IQuicSendStream>(stream);
        const char* data = "Should not be sent";
        send_stream->Send((uint8_t*)data, strlen(data));
        
        buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        server->GenerateSendData(buffer, send_operation);
        EXPECT_EQ(buffer->GetDataLength(), 0);
        
        // Verify server still in Draining state
        EXPECT_EQ(server_base->GetConnectionStateForTest(), ConnectionStateType::kStateDraining);
    }
}

// Test 4: Closing state retransmits CONNECTION_CLOSE on packet reception
TEST_F(ConnectionCloseTest, ClosingStateRetransmitsConnectionClose) {
    auto connections = EstablishConnection();
    auto client = connections.first;
    auto server = connections.second;
    
    auto client_base = std::dynamic_pointer_cast<BaseConnection>(client);
    ASSERT_NE(client_base, nullptr);
    
    // Client initiates close
    client->Close();
    
    // Verify client enters Closing state
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    EXPECT_TRUE(client->GenerateSendData(buffer, send_operation));
    ASSERT_GT(buffer->GetDataLength(), 0);
    
    // Server sends some data to the closing client (simulate late packet)
    auto stream = std::dynamic_pointer_cast<IQuicSendStream>(server->MakeStream(StreamDirection::kSend));
    if (stream) {
        const char* data = "Late data";
        stream->Send((uint8_t*)data, strlen(data));
        
        buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        server->GenerateSendData(buffer, send_operation);
        
        if (buffer->GetDataLength() > 0) {
            std::vector<std::shared_ptr<IPacket>> packets;
            if (DecodePackets(buffer, packets)) {
                // Client receives packet while in Closing state
                client->OnPackets(0, packets);
                
                // Verify client still in Closing state (should not change to Draining on data packets)
                EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
                
                // Client should send a response (CONNECTION_CLOSE retransmission)
                buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
                client->GenerateSendData(buffer, send_operation);
                EXPECT_GT(buffer->GetDataLength(), 0);
            }
        }
    }
}

// Test 5: Draining state does not send packets
TEST_F(ConnectionCloseTest, DrainingStateDoesNotSendPackets) {
    auto connections = EstablishConnection();
    auto client = connections.first;
    auto server = connections.second;
    
    // Client sends CONNECTION_CLOSE
    client->Close();
    
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    EXPECT_TRUE(client->GenerateSendData(buffer, send_operation));
    
    ASSERT_GT(buffer->GetDataLength(), 0);
    
    std::vector<std::shared_ptr<IPacket>> packets;
    ASSERT_TRUE(DecodePackets(buffer, packets));
    
    // Server receives CONNECTION_CLOSE and enters Draining
    server->OnPackets(0, packets);
    
    // Try multiple times to send data, server should remain silent
    for (int i = 0; i < 3; i++) {
        buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
        server->GenerateSendData(buffer, send_operation);
        EXPECT_EQ(buffer->GetDataLength(), 0);
    }
}

// Test 6: Graceful close interrupted by immediate close
TEST_F(ConnectionCloseTest, GracefulCloseInterruptedByImmediateClose) {
    auto connections = EstablishConnection();
    auto client = connections.first;
    auto server = connections.second;
    
    auto client_base = std::dynamic_pointer_cast<BaseConnection>(client);
    ASSERT_NE(client_base, nullptr);
    
    // Verify initial state
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateConnected);
    
    // Create stream with pending data
    auto stream = std::dynamic_pointer_cast<IQuicSendStream>(client->MakeStream(StreamDirection::kSend));
    ASSERT_NE(stream, nullptr);
    
    const char* test_data = "Pending data";
    stream->Send((uint8_t*)test_data, strlen(test_data));
    
    // Start graceful close (may stay in Connected state if pending data)
    client->Close();
    
    // Immediately interrupt with error close using Reset
    const uint64_t error_code = 0x200;
    client->Reset(error_code);
    
    // Verify client enters Closing state (immediate close wins)
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    
    // Verify CONNECTION_CLOSE is sent
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    EXPECT_TRUE(client->GenerateSendData(buffer, send_operation));
    EXPECT_GT(buffer->GetDataLength(), 0);
}

// Test 7: Graceful close interrupted by peer CONNECTION_CLOSE
TEST_F(ConnectionCloseTest, GracefulCloseInterruptedByPeerClose) {
    auto connections = EstablishConnection();
    auto client = connections.first;
    auto server = connections.second;
    
    auto client_base = std::dynamic_pointer_cast<BaseConnection>(client);
    auto server_base = std::dynamic_pointer_cast<BaseConnection>(server);
    ASSERT_NE(client_base, nullptr);
    ASSERT_NE(server_base, nullptr);
    
    // Client creates stream with data
    auto client_stream = std::dynamic_pointer_cast<IQuicSendStream>(client->MakeStream(StreamDirection::kSend));
    ASSERT_NE(client_stream, nullptr);
    
    const char* test_data = "Pending data";
    client_stream->Send((uint8_t*)test_data, strlen(test_data));
    
    // Client starts graceful close (may stay in Connected or go to Closing)
    client->Close();
    auto client_state_after_close = client_base->GetConnectionStateForTest();
    
    // Server sends CONNECTION_CLOSE
    server->Close();
    EXPECT_EQ(server_base->GetConnectionStateForTest(), ConnectionStateType::kStateClosing);
    
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    EXPECT_TRUE(server->GenerateSendData(buffer, send_operation));
    
    ASSERT_GT(buffer->GetDataLength(), 0);
    
    std::vector<std::shared_ptr<IPacket>> packets;
    ASSERT_TRUE(DecodePackets(buffer, packets));
    
    // Client receives peer's CONNECTION_CLOSE
    client->OnPackets(0, packets);
    
    // Client should now be in Draining state (received CONNECTION_CLOSE from peer)
    EXPECT_EQ(client_base->GetConnectionStateForTest(), ConnectionStateType::kStateDraining);
    
    // Client should not send packets in Draining state
    buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    client->GenerateSendData(buffer, send_operation);
    EXPECT_EQ(buffer->GetDataLength(), 0);
}

// Test 8: Connection close during handshake should not crash
TEST_F(ConnectionCloseTest, CloseDuringHandshake) {
    std::shared_ptr<TLSClientCtx> client_ctx = std::make_shared<TLSClientCtx>();
    client_ctx->Init(false);

    auto client = std::make_shared<ClientConnection>(client_ctx, common::MakeTimer(), nullptr, nullptr, nullptr, nullptr, nullptr);

    common::Address addr(common::AddressType::kIpv4);
    addr.SetIp("127.0.0.1");
    addr.SetPort(9432);

    client->Dial(addr, "h3", DEFAULT_QUIC_TRANSPORT_PARAMS);
    
    // Close before handshake completes
    client->Close();
    
    // Should not crash
    uint8_t buf[2000] = {0};
    std::shared_ptr<common::Buffer> buffer = std::make_shared<common::Buffer>(buf, buf + sizeof(buf));
    quic::SendOperation send_operation;
    client->GenerateSendData(buffer, send_operation);
    
    SUCCEED();
}

} // namespace
} // namespace quic
} // namespace quicx
