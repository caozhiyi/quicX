#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>

#include "upgrade/core/upgrade_manager.h"
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/core/protocol_detector.h"
#include "upgrade/core/version_negotiator.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {
namespace {

// Mock TCP socket for integration testing
class MockTcpSocket:
    public ITcpSocket {
public:
    MockTcpSocket() : fd_(123) {}
    explicit MockTcpSocket(int fd) : fd_(fd) {}
    
    virtual int GetFd() const override { return fd_; }
    virtual int Send(const std::vector<uint8_t>& data) override { return data.size(); }
    virtual int Send(const std::string& data) override { return data.size(); }
    virtual int Recv(std::vector<uint8_t>& data, size_t max_size = 4096) override { return 0; }
    virtual int Recv(std::string& data, size_t max_size = 4096) override { return 0; }
    virtual void Close() override { closed_ = true; }
    virtual bool IsValid() const override { return true; }
    virtual std::string GetRemoteAddress() const override { return "127.0.0.1"; }
    virtual uint16_t GetRemotePort() const override { return 8080; }
    virtual void SetHandler(std::shared_ptr<ISocketHandler> handler) override {}
    virtual std::shared_ptr<ISocketHandler> GetHandler() const override { return nullptr; }
    
    bool IsClosed() const { return closed_; }
    
private:
    int fd_;
    bool closed_ = false;
};

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        settings_.http_port = 80;
        settings_.https_port = 443;
        settings_.cert_file = "test.crt";
        settings_.key_file = "test.key";
        
        manager_ = std::make_unique<UpgradeManager>(settings_);
    }
    
    ConnectionContext CreateContext() {
        auto socket = std::make_shared<MockTcpSocket>();
        return ConnectionContext(socket);
    }
    
    UpgradeSettings settings_;
    std::unique_ptr<UpgradeManager> manager_;
};

// Test complete HTTP/1.1 to HTTP/3 upgrade flow
TEST_F(IntegrationTest, CompleteHTTP1ToHTTP3Flow) {
    auto context = CreateContext();
    
    // Step 1: Protocol detection
    std::string http1_request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    context.initial_data = std::vector<uint8_t>(http1_request.begin(), http1_request.end());
    
    Protocol detected = ProtocolDetector::Detect(context.initial_data);
    EXPECT_EQ(detected, Protocol::HTTP1_1);
    
    // Step 2: Set detected protocol
    context.detected_protocol = detected;
    
    // Step 3: Version negotiation
    NegotiationResult negotiation = VersionNegotiator::Negotiate(context, settings_);
    EXPECT_TRUE(negotiation.success);
    EXPECT_EQ(negotiation.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(negotiation.upgrade_data.empty());
    
    // Step 4: Process upgrade
    manager_->ProcessUpgrade(context);
    
    const auto& result = manager_->GetUpgradeResult();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    
    // Step 5: Verify response preparation
    EXPECT_FALSE(context.pending_response.empty());
    EXPECT_EQ(context.response_sent, 0);
}

// Test complete HTTP/2 to HTTP/3 upgrade flow
TEST_F(IntegrationTest, CompleteHTTP2ToHTTP3Flow) {
    auto context = CreateContext();
    
    // Step 1: Protocol detection
    std::string http2_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    context.initial_data = std::vector<uint8_t>(http2_preface.begin(), http2_preface.end());
    
    Protocol detected = ProtocolDetector::Detect(context.initial_data);
    EXPECT_EQ(detected, Protocol::HTTP2);
    
    // Step 2: Set detected protocol
    context.detected_protocol = detected;
    
    // Step 3: Version negotiation
    NegotiationResult negotiation = VersionNegotiator::Negotiate(context, settings_);
    EXPECT_TRUE(negotiation.success);
    EXPECT_EQ(negotiation.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(negotiation.upgrade_data.empty());
    
    // Step 4: Process upgrade
    manager_->ProcessUpgrade(context);
    
    const auto& result = manager_->GetUpgradeResult();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    
    // Step 5: Verify response preparation
    EXPECT_FALSE(context.pending_response.empty());
    EXPECT_EQ(context.response_sent, 0);
}

// Test HTTP/3 direct connection flow
TEST_F(IntegrationTest, HTTP3DirectConnectionFlow) {
    auto context = CreateContext();
    
    // Step 1: Set HTTP/3 protocol directly (no detection needed)
    context.detected_protocol = Protocol::HTTP3;
    
    // Step 2: Version negotiation
    NegotiationResult negotiation = VersionNegotiator::Negotiate(context, settings_);
    EXPECT_TRUE(negotiation.success);
    EXPECT_EQ(negotiation.target_protocol, Protocol::HTTP3);
    EXPECT_TRUE(negotiation.upgrade_data.empty()); // No upgrade needed
    
    // Step 3: Process upgrade
    manager_->ProcessUpgrade(context);
    
    const auto& result = manager_->GetUpgradeResult();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_TRUE(result.upgrade_data.empty());
    
    // Step 4: Verify no response preparation needed
    EXPECT_TRUE(context.pending_response.empty());
    EXPECT_EQ(context.response_sent, 0);
}

// Test unknown protocol flow
TEST_F(IntegrationTest, UnknownProtocolFlow) {
    auto context = CreateContext();
    
    // Step 1: Protocol detection with random data
    std::vector<uint8_t> random_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    context.initial_data = random_data;
    
    Protocol detected = ProtocolDetector::Detect(context.initial_data);
    EXPECT_EQ(detected, Protocol::UNKNOWN);
    
    // Step 2: Set detected protocol
    context.detected_protocol = detected;
    
    // Step 3: Version negotiation
    NegotiationResult negotiation = VersionNegotiator::Negotiate(context, settings_);
    EXPECT_FALSE(negotiation.success);
    EXPECT_EQ(negotiation.target_protocol, Protocol::UNKNOWN);
    EXPECT_TRUE(negotiation.upgrade_data.empty());
    EXPECT_FALSE(negotiation.error_message.empty());
    
    // Step 4: Process upgrade
    manager_->ProcessUpgrade(context);
    
    const auto& result = manager_->GetUpgradeResult();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::UNKNOWN);
    EXPECT_TRUE(result.upgrade_data.empty());
    EXPECT_FALSE(result.error_message.empty());
    
    // Step 5: Verify error response preparation
    EXPECT_FALSE(context.pending_response.empty());
    EXPECT_EQ(context.response_sent, 0);
}

// Test with ALPN protocols
TEST_F(IntegrationTest, WithALPNProtocols) {
    auto context = CreateContext();
    
    // Step 1: Protocol detection
    std::string http1_request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    context.initial_data = std::vector<uint8_t>(http1_request.begin(), http1_request.end());
    
    Protocol detected = ProtocolDetector::Detect(context.initial_data);
    EXPECT_EQ(detected, Protocol::HTTP1_1);
    
    // Step 2: Set detected protocol and ALPN
    context.detected_protocol = detected;
    context.alpn_protocols = {"h3", "h2", "http/1.1"};
    
    // Step 3: Version negotiation
    NegotiationResult negotiation = VersionNegotiator::Negotiate(context, settings_);
    EXPECT_TRUE(negotiation.success);
    EXPECT_EQ(negotiation.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(negotiation.upgrade_data.empty());
    
    // Step 4: Process upgrade
    manager_->ProcessUpgrade(context);
    
    const auto& result = manager_->GetUpgradeResult();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    
    // Step 5: Verify response preparation
    EXPECT_FALSE(context.pending_response.empty());
    EXPECT_EQ(context.response_sent, 0);
}

// Test error handling flow
TEST_F(IntegrationTest, ErrorHandlingFlow) {
    auto context = CreateContext();
    auto socket = std::static_pointer_cast<MockTcpSocket>(context.socket);
    
    // Step 1: Set up context with some data
    context.detected_protocol = Protocol::HTTP1_1;
    
    // Step 2: Simulate an error during processing
    std::string error_message = "Integration test error";
    manager_->HandleUpgradeFailure(context, error_message);
    
    // Step 3: Verify error response preparation
    EXPECT_FALSE(context.pending_response.empty());
    EXPECT_EQ(context.response_sent, 0);
    
    // Step 4: Verify socket was closed
    EXPECT_TRUE(socket->IsClosed());
}

}
} // namespace upgrade
} // namespace quicx 