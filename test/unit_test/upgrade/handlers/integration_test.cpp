#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <atomic>

#include "upgrade/include/type.h"
#include "upgrade/network/tcp_socket.h"
#include "common/network/if_event_loop.h"
#include "upgrade/handlers/connection_context.h"
#include "upgrade/handlers/http_smart_handler.h"
#include "upgrade/handlers/https_smart_handler.h"
#include "upgrade/handlers/smart_handler_factory.h"

namespace quicx {
namespace upgrade {
namespace {

// Mock event loop for integration testing
class MockEventLoop:
    public common::IEventLoop {
public:
    MockEventLoop() : init_called_(false), wakeup_called_(false) {}
    
    virtual bool Init() override {
        init_called_ = true;
        return true;
    }
    
    int Wait() override { return 0; }

    bool RegisterFd(uint32_t, int32_t, std::shared_ptr<common::IFdHandler>) override { return true; }
    bool ModifyFd(uint32_t sockfd, int32_t events) override {
        modify_calls_.push_back({static_cast<int>(sockfd), events});
        return true;
    }
    bool RemoveFd(uint32_t) override { return true; }

    virtual void AddFixedProcess(std::function<void()>) override {
        return;
    }

    virtual uint64_t AddTimer(std::function<void()> callback, uint32_t, bool = false) override {
        timer_callbacks_.push_back(callback);
        return next_timer_id_++;
    }

    virtual uint64_t AddTimer(common::TimerTask& task, uint32_t, bool = false) override {
        return 0;
    }
    virtual bool RemoveTimer(uint64_t) override {
        return true;
    }
    
    virtual bool RemoveTimer(common::TimerTask& task) override {
        return true;
    }
    
    virtual void SetTimerForTest(std::shared_ptr<common::ITimer> timer) override {
        return;
    }
    
    virtual void PostTask(std::function<void()>) override {}

    virtual void Wakeup() override {
        wakeup_called_ = true;
    }

    virtual std::shared_ptr<common::ITimer> GetTimer() override {
        return nullptr;
    }
    
    virtual bool IsInLoopThread() const override {
        return true;  // Mock always returns true for testing
    }
    
    virtual void RunInLoop(std::function<void()> task) override {
        if (task) {
            task();
        }
    }
    
    virtual void AssertInLoopThread() override {
        // Mock implementation - does nothing in test
    }
    
    // Test helper methods
    bool IsInitCalled() const { return init_called_; }
    bool IsWakeupCalled() const { return wakeup_called_; }
    const std::vector<std::pair<int, int32_t>>& GetModifyCalls() const { return modify_calls_; }
    const std::vector<std::function<void()>>& GetTimerCallbacks() const { return timer_callbacks_; }
    
private:
    std::atomic<bool> init_called_;
    std::atomic<bool> wakeup_called_;
    std::vector<std::pair<int, int32_t>> modify_calls_;
    std::vector<std::function<void()>> timer_callbacks_;
    uint64_t next_timer_id_ = 1;
};

class HandlersIntegrationTest:
    public ::testing::Test {
protected:
    void SetUp() override {
        factory_ = std::make_unique<SmartHandlerFactory>();
        event_loop_ = std::make_shared<MockEventLoop>();
    }
    
    void TearDown() override {
        factory_.reset();
        event_loop_.reset();
    }
    
    std::unique_ptr<SmartHandlerFactory> factory_;
    std::shared_ptr<MockEventLoop> event_loop_;
};

// Test complete HTTP handler lifecycle
TEST_F(HandlersIntegrationTest, CompleteHttpHandlerLifecycle) {
    // Create HTTP settings
    UpgradeSettings settings;
    settings.http_port = 8080;
    settings.https_port = 0;

    // Create HTTP handler
    auto handler = factory_->CreateHandler(settings, event_loop_);
    EXPECT_NE(handler, nullptr);
    EXPECT_EQ(handler->GetType(), "HTTP");
    
    // Cast to HTTP handler
    auto http_handler = std::dynamic_pointer_cast<HttpSmartHandler>(handler);
    EXPECT_NE(http_handler, nullptr);
    
    // Create socket
    auto socket = std::make_shared<TcpSocket>();
    EXPECT_TRUE(socket->IsValid());
    
    // Handle connection
    handler->OnConnect(static_cast<uint32_t>(socket->GetFd()));
    
    // Handle read
    handler->OnRead(static_cast<uint32_t>(socket->GetFd()));
    
    // Handle write
    handler->OnWrite(static_cast<uint32_t>(socket->GetFd()));
    
    // Handle close
    handler->OnClose(static_cast<uint32_t>(socket->GetFd()));
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
    auto handler = factory_->CreateHandler(settings, event_loop_);
    EXPECT_NE(handler, nullptr);
    EXPECT_EQ(handler->GetType(), "HTTPS");
    
    // Cast to HTTPS handler
    auto https_handler = std::dynamic_pointer_cast<HttpsSmartHandler>(handler);
    EXPECT_NE(https_handler, nullptr);
    
    // Create socket
    auto socket = std::make_shared<TcpSocket>();
    EXPECT_TRUE(socket->IsValid());
    
    // Handle connection
    handler->OnConnect(static_cast<uint32_t>(socket->GetFd()));
    
    // Handle read
    handler->OnRead(static_cast<uint32_t>(socket->GetFd()));
    
    // Handle write
    handler->OnWrite(static_cast<uint32_t>(socket->GetFd()));
    
    // Handle close
    handler->OnClose(static_cast<uint32_t>(socket->GetFd()));
}

// Test connection context integration
TEST_F(HandlersIntegrationTest, ConnectionContextIntegration) {
    // Create HTTP handler
    UpgradeSettings settings;
    settings.http_port = 8080;
    settings.https_port = 0;
    
    auto handler = factory_->CreateHandler(settings, event_loop_);
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
    
    auto http_handler = factory_->CreateHandler(http_settings, event_loop_);
    EXPECT_NE(http_handler, nullptr);
    EXPECT_EQ(http_handler->GetType(), "HTTP");
    
    // Create HTTPS handler
    UpgradeSettings https_settings;
    https_settings.http_port = 0;
    https_settings.https_port = 8443;
    https_settings.cert_file = "test.crt";
    https_settings.key_file = "test.key";
    
    auto https_handler = factory_->CreateHandler(https_settings, event_loop_);
    EXPECT_NE(https_handler, nullptr);
    EXPECT_EQ(https_handler->GetType(), "HTTPS");
    
    // Handlers should be different instances
    EXPECT_NE(http_handler, https_handler);
    
    // Test both handlers with sockets
    auto socket1 = std::make_shared<TcpSocket>();
    auto socket2 = std::make_shared<TcpSocket>();
    
    http_handler->OnConnect(static_cast<uint32_t>(socket1->GetFd()));
    https_handler->OnConnect(static_cast<uint32_t>(socket2->GetFd()));
}

// Test handler with event driver integration
TEST_F(HandlersIntegrationTest, HandlerWithEventDriver) {
    // Create HTTP handler
    UpgradeSettings settings;
    settings.http_port = 8080;
    settings.https_port = 0;
    
    auto handler = factory_->CreateHandler(settings, event_loop_);
    auto socket = std::make_shared<TcpSocket>();
    
    // Set event driver (if accessible)
    // Note: This depends on the specific implementation of the handler
    
    // Handle connection
    handler->OnConnect(static_cast<uint32_t>(socket->GetFd()));
    
    // Handle operations
    handler->OnRead(static_cast<uint32_t>(socket->GetFd()));
    handler->OnWrite(static_cast<uint32_t>(socket->GetFd()));
    handler->OnClose(static_cast<uint32_t>(socket->GetFd()));
}

// Test handler error handling
TEST_F(HandlersIntegrationTest, HandlerErrorHandling) {
    // Create handler with invalid settings
    UpgradeSettings settings;
    settings.http_port = 0;
    settings.https_port = 0;
    
    auto handler = factory_->CreateHandler(settings, event_loop_);
    EXPECT_NE(handler, nullptr);
    
    // Should default to HTTP handler
    EXPECT_EQ(handler->GetType(), "HTTP");
    
    // Test with invalid socket
    auto invalid_socket = std::make_shared<TcpSocket>(-1);
    EXPECT_FALSE(invalid_socket->IsValid());
    
    // Handler should handle invalid socket gracefully
    handler->OnConnect(static_cast<uint32_t>(invalid_socket->GetFd()));
    handler->OnRead(static_cast<uint32_t>(invalid_socket->GetFd()));
    handler->OnWrite(static_cast<uint32_t>(invalid_socket->GetFd()));
    handler->OnClose(static_cast<uint32_t>(invalid_socket->GetFd()));
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
    
    auto both_handler = factory_->CreateHandler(both_settings, event_loop_);
    EXPECT_NE(both_handler, nullptr);
    EXPECT_EQ(both_handler->GetType(), "HTTPS"); // Should prefer HTTPS
    
    // Test with only HTTP port
    UpgradeSettings http_only_settings;
    http_only_settings.http_port = 8080;
    http_only_settings.https_port = 0;
    
    auto http_only_handler = factory_->CreateHandler(http_only_settings, event_loop_);
    EXPECT_NE(http_only_handler, nullptr);
    EXPECT_EQ(http_only_handler->GetType(), "HTTP");
    
    // Test with only HTTPS port but no certificates
    UpgradeSettings https_no_cert_settings;
    https_no_cert_settings.http_port = 0;
    https_no_cert_settings.https_port = 8443;
    
    auto https_no_cert_handler = factory_->CreateHandler(https_no_cert_settings, event_loop_);
    EXPECT_NE(https_no_cert_handler, nullptr);
    EXPECT_EQ(https_no_cert_handler->GetType(), "HTTP"); // Should fall back to HTTP
}

}
} // namespace upgrade
} // namespace quicx 