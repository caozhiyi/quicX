#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include "upgrade/core/upgrade_manager.h"
#include "upgrade/handlers/connection_context.h"
#include "upgrade/network/if_tcp_socket.h"

namespace quicx {
namespace upgrade {

// Mock TCP socket for testing
class MockTcpSocket : public ITcpSocket {
public:
    MockTcpSocket() : fd_(123) {}
    explicit MockTcpSocket(int fd) : fd_(fd) {}
    
    virtual int GetFd() const override { return fd_; }
    virtual int Send(const std::vector<uint8_t>& data) override { 
        last_sent_data_ = data;
        return data.size(); 
    }
    virtual int Send(const std::string& data) override { 
        last_sent_string_ = data;
        return data.size(); 
    }
    virtual int Recv(std::vector<uint8_t>& data, size_t max_size = 4096) override { return 0; }
    virtual int Recv(std::string& data, size_t max_size = 4096) override { return 0; }
    virtual void Close() override { closed_ = true; }
    virtual bool IsValid() const override { return !closed_; }
    virtual std::string GetRemoteAddress() const override { return "127.0.0.1"; }
    virtual uint16_t GetRemotePort() const override { return 8080; }
    virtual std::string GetLocalAddress() const override { return "127.0.0.1"; }
    virtual uint16_t GetLocalPort() const override { return 80; }
    virtual void SetHandler(std::shared_ptr<ISocketHandler> handler) override {}
    virtual std::shared_ptr<ISocketHandler> GetHandler() const override { return nullptr; }
    
    // Test helper methods
    const std::vector<uint8_t>& GetLastSentData() const { return last_sent_data_; }
    const std::string& GetLastSentString() const { return last_sent_string_; }
    bool IsClosed() const { return closed_; }
    
private:
    int fd_;
    std::vector<uint8_t> last_sent_data_;
    std::string last_sent_string_;
    bool closed_ = false;
};

class UpgradeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        settings_.http_port = 80;
        settings_.https_port = 443;
        settings_.cert_file = "test.crt";
        settings_.key_file = "test.key";
        
        manager_ = std::make_unique<UpgradeManager>(settings_);
    }
    
    ConnectionContext CreateContext(Protocol detected_protocol = Protocol::UNKNOWN) {
        auto socket = std::make_shared<MockTcpSocket>();
        ConnectionContext context(socket);
        context.detected_protocol = detected_protocol;
        return context;
    }
    
    UpgradeSettings settings_;
    std::unique_ptr<UpgradeManager> manager_;
};

// Test successful upgrade processing
TEST_F(UpgradeManagerTest, SuccessfulUpgrade) {
    auto context = CreateContext(Protocol::HTTP1_1);
    
    std::string http1_request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    context.initial_data = std::vector<uint8_t>(http1_request.begin(), http1_request.end());
    
    manager_->ProcessUpgrade(context);
    
    const auto& result = manager_->GetUpgradeResult();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::HTTP3);
    EXPECT_FALSE(result.upgrade_data.empty());
    EXPECT_TRUE(result.error_message.empty());
    
    EXPECT_FALSE(context.pending_response.empty());
    EXPECT_EQ(context.response_sent, 0);
}

// Test upgrade failure handling
TEST_F(UpgradeManagerTest, HandleUpgradeFailure) {
    auto context = CreateContext(Protocol::HTTP1_1);
    auto socket = std::static_pointer_cast<MockTcpSocket>(context.socket);
    
    std::string error_message = "Test error message";
    manager_->HandleUpgradeFailure(context, error_message);
    
    EXPECT_FALSE(context.pending_response.empty());
    EXPECT_EQ(context.response_sent, 0);
    EXPECT_TRUE(socket->IsClosed());
}

// Test unknown protocol
TEST_F(UpgradeManagerTest, UnknownProtocol) {
    auto context = CreateContext(Protocol::UNKNOWN);
    
    manager_->ProcessUpgrade(context);
    
    const auto& result = manager_->GetUpgradeResult();
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.target_protocol, Protocol::UNKNOWN);
    EXPECT_TRUE(result.upgrade_data.empty());
    EXPECT_FALSE(result.error_message.empty());
}

} // namespace upgrade
} // namespace quicx 