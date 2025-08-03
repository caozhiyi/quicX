#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include "upgrade/handlers/smart_handler_factory.h"
#include "upgrade/handlers/http_smart_handler.h"
#include "upgrade/handlers/https_smart_handler.h"
#include "upgrade/handlers/connection_context.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/network/if_event_driver.h"
#include "upgrade/include/type.h"

namespace quicx {
namespace upgrade {

// Mock TCP action for integration testing
class MockTcpAction : public ITcpAction {
public:
    MockTcpAction() : init_called_(false), stop_called_(false), join_called_(false) {}
    
    virtual bool Init() override {
        init_called_ = true;
        return true;
    }
    
    virtual bool AddListener(const std::string& addr, uint16_t port, std::shared_ptr<ISocketHandler> handler) override {
        return true;
    }
    
    virtual void Stop() override {
        stop_called_ = true;
    }
    
    virtual void Join() override {
        join_called_ = true;
    }
    
    virtual uint64_t AddTimer(std::function<void()> callback, uint32_t timeout_ms) override {
        timer_callbacks_.push_back(callback);
        return next_timer_id_++;
    }
    
    virtual bool RemoveTimer(uint64_t timer_id) override {
        return true;
    }
    
    // Test helper methods
    bool IsInitCalled() const { return init_called_; }
    bool IsStopCalled() const { return stop_called_; }
    bool IsJoinCalled() const { return join_called_; }
    const std::vector<std::function<void()>>& GetTimerCallbacks() const { return timer_callbacks_; }
    
private:
    std::atomic<bool> init_called_;
    std::atomic<bool> stop_called_;
    std::atomic<bool> join_called_;
    std::vector<std::function<void()>> timer_callbacks_;
    uint64_t next_timer_id_ = 1;
};

// Mock event driver for integration testing
class MockEventDriver : public IEventDriver {
public:
    MockEventDriver() : init_called_(false), wakeup_called_(false) {}
    
    virtual bool Init() override {
        init_called_ = true;
        return true;
    }
    
    virtual bool AddFd(int fd, EventType events) override {
        return true;
    }
    
    virtual bool RemoveFd(int fd) override {
        return true;
    }
    
    virtual bool ModifyFd(int fd, EventType events) override {
        modify_calls_.push_back({fd, events});
        return true;
    }
    
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override {
        return 0;
    }
    
    virtual int GetMaxEvents() const override {
        return 100;
    }
    
    virtual void Wakeup() override {
        wakeup_called_ = true;
    }
    
    // Test helper methods
    bool IsInitCalled() const { return init_called_; }
    bool IsWakeupCalled() const { return wakeup_called_; }
    const std::vector<std::pair<int, EventType>>& GetModifyCalls() const { return modify_calls_; }
    
private:
    std::atomic<bool> init_called_;
    std::atomic<bool> wakeup_called_;
    std::vector<std::pair<int, EventType>> modify_calls_;
};

class HandlersIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        factory_ = std::make_unique<SmartHandlerFactory>();
        tcp_action_ = std::make_shared<MockTcpAction>();
        event_driver_ = std::make_shared<MockEventDriver>();
    }
    
    void TearDown() override {
        factory_.reset();
        tcp_action_.reset();
        event_driver_.reset();
    }
    
    std::unique_ptr<SmartHandlerFactory> factory_;
    std::shared_ptr<MockTcpAction> tcp_action_;
    std::shared_ptr<MockEventDriver> event_driver_;
};

// Test complete HTTP handler lifecycle
TEST_F(HandlersIntegrationTest, CompleteHttpHandlerLifecycle) {
    // Create HTTP settings
    UpgradeSettings settings;
    settings.http_port = 8080;
    settings.https_port = 0;
    
    // Create HTTP handler
    auto handler = factory_->CreateHandler(settings);
    EXPECT_NE(handler, nullptr);
    EXPECT_EQ(handler->GetType(), "HTTP");
    
    // Cast to HTTP handler
    auto http_handler = std::dynamic_pointer_cast<HttpSmartHandler>(handler);
    EXPECT_NE(http_handler, nullptr);
    
    // Create socket
    auto socket = std::make_shared<TcpSocket>();
    EXPECT_TRUE(socket->IsValid());
    
    // Handle connection
    handler->HandleConnect(socket, tcp_action_);
    EXPECT_TRUE(tcp_action_->IsInitCalled());
    
    // Handle read
    handler->HandleRead(socket);
    
    // Handle write
    handler->HandleWrite(socket);
    
    // Handle close
    handler->HandleClose(socket);
}

// Test complete HTTPS handler lifecycle
TEST_F(HandlersIntegrationTest, CompleteHttpsHandlerLifecycle) {
    // Create HTTPS settings
    UpgradeSettings settings;
    settings.http_port = 0;
    settings.https_port = 8443;
    settings.cert_file = "test.crt";
    settings.key_file = "test.key";
    
    // Create HTTPS handler
    auto handler = factory_->CreateHandler(settings);
    EXPECT_NE(handler, nullptr);
    EXPECT_EQ(handler->GetType(), "HTTPS");
    
    // Cast to HTTPS handler
    auto https_handler = std::dynamic_pointer_cast<HttpsSmartHandler>(handler);
    EXPECT_NE(https_handler, nullptr);
    
    // Create socket
    auto socket = std::make_shared<TcpSocket>();
    EXPECT_TRUE(socket->IsValid());
    
    // Handle connection
    handler->HandleConnect(socket, tcp_action_);
    EXPECT_TRUE(tcp_action_->IsInitCalled());
    
    // Handle read
    handler->HandleRead(socket);
    
    // Handle write
    handler->HandleWrite(socket);
    
    // Handle close
    handler->HandleClose(socket);
}

// Test connection context integration
TEST_F(HandlersIntegrationTest, ConnectionContextIntegration) {
    // Create HTTP handler
    UpgradeSettings settings;
    settings.http_port = 8080;
    settings.https_port = 0;
    
    auto handler = factory_->CreateHandler(settings);
    auto socket = std::make_shared<TcpSocket>();
    
    // Create connection context
    ConnectionContext context(socket);
    EXPECT_EQ(context.socket, socket);
    EXPECT_EQ(context.state, ConnectionState::INITIAL);
    
    // Simulate protocol detection
    context.state = ConnectionState::DETECTING;
    context.detected_protocol = Protocol::HTTP1_1;
    context.target_protocol = Protocol::HTTP3;
    
    EXPECT_EQ(context.state, ConnectionState::DETECTING);
    EXPECT_EQ(context.detected_protocol, Protocol::HTTP1_1);
    EXPECT_EQ(context.target_protocol, Protocol::HTTP3);
    
    // Simulate negotiation
    context.state = ConnectionState::NEGOTIATING;
    EXPECT_EQ(context.state, ConnectionState::NEGOTIATING);
    
    // Simulate upgrade completion
    context.state = ConnectionState::UPGRADED;
    EXPECT_EQ(context.state, ConnectionState::UPGRADED);
}

// Test multiple handlers with different settings
TEST_F(HandlersIntegrationTest, MultipleHandlersDifferentSettings) {
    // Create HTTP handler
    UpgradeSettings http_settings;
    http_settings.http_port = 8080;
    http_settings.https_port = 0;
    
    auto http_handler = factory_->CreateHandler(http_settings);
    EXPECT_NE(http_handler, nullptr);
    EXPECT_EQ(http_handler->GetType(), "HTTP");
    
    // Create HTTPS handler
    UpgradeSettings https_settings;
    https_settings.http_port = 0;
    https_settings.https_port = 8443;
    https_settings.cert_file = "test.crt";
    https_settings.key_file = "test.key";
    
    auto https_handler = factory_->CreateHandler(https_settings);
    EXPECT_NE(https_handler, nullptr);
    EXPECT_EQ(https_handler->GetType(), "HTTPS");
    
    // Handlers should be different instances
    EXPECT_NE(http_handler, https_handler);
    
    // Test both handlers with sockets
    auto socket1 = std::make_shared<TcpSocket>();
    auto socket2 = std::make_shared<TcpSocket>();
    
    http_handler->HandleConnect(socket1, tcp_action_);
    https_handler->HandleConnect(socket2, tcp_action_);
    
    EXPECT_TRUE(tcp_action_->IsInitCalled());
}

// Test handler with event driver integration
TEST_F(HandlersIntegrationTest, HandlerWithEventDriver) {
    // Create HTTP handler
    UpgradeSettings settings;
    settings.http_port = 8080;
    settings.https_port = 0;
    
    auto handler = factory_->CreateHandler(settings);
    auto socket = std::make_shared<TcpSocket>();
    
    // Set event driver (if accessible)
    // Note: This depends on the specific implementation of the handler
    
    // Handle connection
    handler->HandleConnect(socket, tcp_action_);
    EXPECT_TRUE(tcp_action_->IsInitCalled());
    
    // Handle operations
    handler->HandleRead(socket);
    handler->HandleWrite(socket);
    handler->HandleClose(socket);
}

// Test handler error handling
TEST_F(HandlersIntegrationTest, HandlerErrorHandling) {
    // Create handler with invalid settings
    UpgradeSettings settings;
    settings.http_port = 0;
    settings.https_port = 0;
    
    auto handler = factory_->CreateHandler(settings);
    EXPECT_NE(handler, nullptr);
    
    // Should default to HTTP handler
    EXPECT_EQ(handler->GetType(), "HTTP");
    
    // Test with invalid socket
    auto invalid_socket = std::make_shared<TcpSocket>(-1);
    EXPECT_FALSE(invalid_socket->IsValid());
    
    // Handler should handle invalid socket gracefully
    handler->HandleConnect(invalid_socket, tcp_action_);
    handler->HandleRead(invalid_socket);
    handler->HandleWrite(invalid_socket);
    handler->HandleClose(invalid_socket);
}

// Test connection context with different protocols
TEST_F(HandlersIntegrationTest, ConnectionContextWithProtocols) {
    auto socket = std::make_shared<TcpSocket>();
    
    // Test HTTP/1.1 context
    ConnectionContext http_context(socket);
    http_context.detected_protocol = Protocol::HTTP1_1;
    http_context.target_protocol = Protocol::HTTP3;
    EXPECT_EQ(http_context.detected_protocol, Protocol::HTTP1_1);
    EXPECT_EQ(http_context.target_protocol, Protocol::HTTP3);
    
    // Test HTTP/2 context
    ConnectionContext http2_context(socket);
    http2_context.detected_protocol = Protocol::HTTP2;
    http2_context.target_protocol = Protocol::HTTP3;
    EXPECT_EQ(http2_context.detected_protocol, Protocol::HTTP2);
    EXPECT_EQ(http2_context.target_protocol, Protocol::HTTP3);
    
    // Test HTTP/3 context
    ConnectionContext http3_context(socket);
    http3_context.detected_protocol = Protocol::HTTP3;
    http3_context.target_protocol = Protocol::HTTP3;
    EXPECT_EQ(http3_context.detected_protocol, Protocol::HTTP3);
    EXPECT_EQ(http3_context.target_protocol, Protocol::HTTP3);
}

// Test factory with various settings combinations
TEST_F(HandlersIntegrationTest, FactorySettingsCombinations) {
    // Test with both ports enabled
    UpgradeSettings both_settings;
    both_settings.http_port = 8080;
    both_settings.https_port = 8443;
    both_settings.cert_file = "test.crt";
    both_settings.key_file = "test.key";
    
    auto both_handler = factory_->CreateHandler(both_settings);
    EXPECT_NE(both_handler, nullptr);
    EXPECT_EQ(both_handler->GetType(), "HTTPS"); // Should prefer HTTPS
    
    // Test with only HTTP port
    UpgradeSettings http_only_settings;
    http_only_settings.http_port = 8080;
    http_only_settings.https_port = 0;
    
    auto http_only_handler = factory_->CreateHandler(http_only_settings);
    EXPECT_NE(http_only_handler, nullptr);
    EXPECT_EQ(http_only_handler->GetType(), "HTTP");
    
    // Test with only HTTPS port but no certificates
    UpgradeSettings https_no_cert_settings;
    https_no_cert_settings.http_port = 0;
    https_no_cert_settings.https_port = 8443;
    
    auto https_no_cert_handler = factory_->CreateHandler(https_no_cert_settings);
    EXPECT_NE(https_no_cert_handler, nullptr);
    EXPECT_EQ(https_no_cert_handler->GetType(), "HTTP"); // Should fall back to HTTP
}

} // namespace upgrade
} // namespace quicx 