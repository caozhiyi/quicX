#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/core/version_negotiator.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {
namespace {

// Mock TCP socket for testing
class MockTcpSocket : public ITcpSocket {
public:
    MockTcpSocket() : fd_(123) {}
    explicit MockTcpSocket(int fd) : fd_(fd) {}
    
    virtual int GetFd() const override { return fd_; }
    virtual int Send(const std::vector<uint8_t>& data) override { return data.size(); }
    virtual int Send(const std::string& data) override { return data.size(); }
    virtual int Recv(std::vector<uint8_t>& data, size_t max_size = 4096) override { return 0; }
    virtual int Recv(std::string& data, size_t max_size = 4096) override { return 0; }
    virtual void Close() override {}
    virtual bool IsValid() const override { return true; }
    virtual std::string GetRemoteAddress() const override { return "127.0.0.1"; }
    virtual uint16_t GetRemotePort() const override { return 8080; }
    virtual void SetHandler(std::shared_ptr<ISocketHandler> handler) override {}
    virtual std::shared_ptr<ISocketHandler> GetHandler() const override { return nullptr; }
    
private:
    int fd_;
};

class VersionNegotiatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test fixtures
        settings_.http_port = 80;
        settings_.https_port = 443;
        settings_.cert_file = "test.crt";
        settings_.key_file = "test.key";
    }
    
    void TearDown() override {
        // Clean up test fixtures
    }
    
    ConnectionContext CreateContext(Protocol detected_protocol = Protocol::UNKNOWN) {
        auto socket = std::make_shared<MockTcpSocket>();
        ConnectionContext context(socket);
        context.detected_protocol = detected_protocol;
        return context;
    }
    
    UpgradeSettings settings_;
};

// Test successful HTTP/1.1 to HTTP/3 upgrade
TEST_F(VersionNegotiatorTest, HTTP1ToHTTP3Upgrade) {
    auto context = CreateContext(Protocol::HTTP1_1);
    
    // Simulate HTTP/1.1 request data
    std::string http1_request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    context.initial_data = std::vector<uint8_t>(http1_request.begin(), http1_request.end());
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test successful HTTP/2 to HTTP/3 upgrade
TEST_F(VersionNegotiatorTest, HTTP2ToHTTP3Upgrade) {
    auto context = CreateContext(Protocol::HTTP2);
    
    // Simulate HTTP/2 connection preface
    std::string http2_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    context.initial_data = std::vector<uint8_t>(http2_preface.begin(), http2_preface.end());
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test HTTP/3 direct connection
TEST_F(VersionNegotiatorTest, HTTP3DirectConnection) {
    auto context = CreateContext(Protocol::HTTP3);
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_TRUE(result.upgrade_data.empty()); // No upgrade needed
    EXPECT_TRUE(result.error_message.empty());
}

// Test unknown protocol
TEST_F(VersionNegotiatorTest, UnknownProtocol) {
    auto context = CreateContext(Protocol::UNKNOWN);
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::UNKNOWN);
    EXPECT_TRUE(result.upgrade_data.empty());
    EXPECT_FALSE(result.error_message.empty());
}

// Test with ALPN protocols
TEST_F(VersionNegotiatorTest, WithALPNProtocols) {
    auto context = CreateContext(Protocol::HTTP1_1);
    context.alpn_protocols = {"h3", "h2", "http/1.1"};
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test with ALPN protocols preferring HTTP/2
TEST_F(VersionNegotiatorTest, ALPNPreferHTTP2) {
    auto context = CreateContext(Protocol::HTTP1_1);
    context.alpn_protocols = {"h2", "http/1.1"}; // No h3
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3); // Still prefer HTTP/3
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test with empty ALPN protocols
TEST_F(VersionNegotiatorTest, EmptyALPNProtocols) {
    auto context = CreateContext(Protocol::HTTP1_1);
    context.alpn_protocols.clear();
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test HTTPS settings
TEST_F(VersionNegotiatorTest, HTTPSEnabled) {
    settings_.cert_file = "test.crt";
    settings_.key_file = "test.key";
    
    auto context = CreateContext(Protocol::HTTP1_1);
    context.alpn_protocols = {"h3"};
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test HTTPS settings with no ALPN
TEST_F(VersionNegotiatorTest, HTTPSNoALPN) {
    settings_.cert_file = "test.crt";
    settings_.key_file = "test.key";
    
    auto context = CreateContext(Protocol::HTTP1_1);
    context.alpn_protocols.clear();
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test with headers containing upgrade information
TEST_F(VersionNegotiatorTest, WithUpgradeHeaders) {
    auto context = CreateContext(Protocol::HTTP1_1);
    context.headers["Upgrade"] = "h3";
    context.headers["Connection"] = "Upgrade";
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test with invalid upgrade headers
TEST_F(VersionNegotiatorTest, InvalidUpgradeHeaders) {
    auto context = CreateContext(Protocol::HTTP1_1);
    context.headers["Upgrade"] = "websocket"; // Not HTTP/3
    context.headers["Connection"] = "Upgrade";
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, settings_);
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3); // Still upgrade to HTTP/3
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

// Test error handling with invalid settings
TEST_F(VersionNegotiatorTest, InvalidSettings) {
    UpgradeSettings invalid_settings;
    invalid_settings.http_port = 0;
    invalid_settings.https_port = 0;
    
    auto context = CreateContext(Protocol::HTTP1_1);
    
    NegotiationResult result = VersionNegotiator::Negotiate(context, invalid_settings);
    
    // Should still work as we always prefer HTTP/3
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
}

}
} // namespace upgrade
} // namespace quicx 