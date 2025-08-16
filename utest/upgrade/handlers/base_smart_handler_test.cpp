#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <atomic>

#include "upgrade/include/type.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/network/if_event_driver.h"
#include "upgrade/handlers/base_smart_handler.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {
namespace {

// Mock TCP action for testing
class MockTcpAction:
    public ITcpAction {
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

// Mock event driver for testing
class MockEventDriver:
    public IEventDriver {
public:
    MockEventDriver() : init_called_(false), wakeup_called_(false) {}
    
    virtual bool Init() override {
        init_called_ = true;
        return true;
    }
    
    virtual bool AddFd(uint64_t fd, EventType events) override {
        return true;
    }
    
    virtual bool RemoveFd(uint64_t fd) override {
        return true;
    }
    
    virtual bool ModifyFd(uint64_t fd, EventType events) override {
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

// Concrete implementation of BaseSmartHandler for testing
class TestSmartHandler:
    public BaseSmartHandler {
public:
    explicit TestSmartHandler(const UpgradeSettings& settings, std::shared_ptr<ITcpAction> tcp_action):
        BaseSmartHandler(settings, tcp_action),
        init_called_(false),
        read_called_(false),
        write_called_(false),
        cleanup_called_(false) {}
    
    virtual bool InitializeConnection(std::shared_ptr<ITcpSocket> socket) override {
        init_called_ = true;
        return true;
    }
    
    virtual int ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) override {
        read_called_ = true;
        data = {0x48, 0x54, 0x54, 0x50}; // "HTTP"
        return data.size();
    }
    
    virtual int WriteData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) override {
        write_called_ = true;
        return data.size();
    }
    
    virtual void CleanupConnection(std::shared_ptr<ITcpSocket> socket) override {
        cleanup_called_ = true;
    }
    
    virtual std::string GetType() const override {
        return "TEST";
    }
    
    // Test helper methods
    bool IsInitCalled() const { return init_called_; }
    bool IsReadCalled() const { return read_called_; }
    bool IsWriteCalled() const { return write_called_; }
    bool IsCleanupCalled() const { return cleanup_called_; }
    
    // Expose protected methods for testing
    using BaseSmartHandler::SetEventDriver;
    using BaseSmartHandler::HandleProtocolDetection;
    using BaseSmartHandler::OnUpgradeComplete;
    using BaseSmartHandler::OnUpgradeFailed;
    using BaseSmartHandler::HandleNegotiationTimeout;
    using BaseSmartHandler::TrySendResponse;
    
private:
    std::atomic<bool> init_called_;
    std::atomic<bool> read_called_;
    std::atomic<bool> write_called_;
    std::atomic<bool> cleanup_called_;
};

class BaseSmartHandlerTest:
    public ::testing::Test {
protected:
    void SetUp() override {
        settings_.http_port = 8080;
        settings_.https_port = 0;
        
        handler_ = std::make_unique<TestSmartHandler>(settings_, tcp_action_);
        socket_ = std::make_shared<TcpSocket>();
        tcp_action_ = std::make_shared<MockTcpAction>();
        event_driver_ = std::make_shared<MockEventDriver>();
        
        handler_->SetEventDriver(event_driver_);
    }
    
    void TearDown() override {
        handler_.reset();
        socket_.reset();
        tcp_action_.reset();
        event_driver_.reset();
    }
    
    UpgradeSettings settings_;
    std::unique_ptr<TestSmartHandler> handler_;
    std::shared_ptr<ITcpSocket> socket_;
    std::shared_ptr<MockTcpAction> tcp_action_;
    std::shared_ptr<MockEventDriver> event_driver_;
};

// Test handler creation
TEST_F(BaseSmartHandlerTest, HandlerCreation) {
    EXPECT_NE(handler_, nullptr);
    EXPECT_EQ(handler_->GetType(), "TEST");
}

// Test handle connect
TEST_F(BaseSmartHandlerTest, HandleConnect) {
    handler_->HandleConnect(socket_);
    
    EXPECT_TRUE(handler_->IsInitCalled());
    EXPECT_FALSE(tcp_action_->IsInitCalled());  // Init should not be called for individual connections
    
    // Check if connection context was created
    // Note: We can't directly access connections_ map, but we can verify
    // that the handler processed the connection
}

// Test handle read
TEST_F(BaseSmartHandlerTest, HandleRead) {
    // First connect
    handler_->HandleConnect(socket_);
    
    // Then read
    handler_->HandleRead(socket_);
    
    EXPECT_TRUE(handler_->IsReadCalled());
}

// Test handle write
TEST_F(BaseSmartHandlerTest, HandleWrite) {
    // First connect
    handler_->HandleConnect(socket_);
    
    // Then write - this won't call WriteData unless in NEGOTIATING state
    handler_->HandleWrite(socket_);
    
    EXPECT_FALSE(handler_->IsWriteCalled());  // WriteData is only called during negotiation
}

// Test handle close
TEST_F(BaseSmartHandlerTest, HandleClose) {
    // First connect
    handler_->HandleConnect(socket_);
    
    // Then close
    handler_->HandleClose(socket_);
    
    EXPECT_TRUE(handler_->IsCleanupCalled());
}

// Test protocol detection
TEST_F(BaseSmartHandlerTest, ProtocolDetection) {
    handler_->HandleConnect(socket_);
    
    // Simulate reading HTTP data
    std::vector<uint8_t> http_data = {0x48, 0x54, 0x54, 0x50, 0x2F, 0x31, 0x2E, 0x31}; // "HTTP/1.1"
    handler_->HandleProtocolDetection(socket_, http_data);
    
    // Protocol detection should be triggered
    // Note: We can't directly verify the internal state, but we can ensure
    // the method doesn't crash and processes the data
}

// Test upgrade completion
TEST_F(BaseSmartHandlerTest, UpgradeCompletion) {
    handler_->HandleConnect(socket_);
    
    // Create a connection context
    ConnectionContext context(socket_);
    context.state = ConnectionState::NEGOTIATING;
    
    // Test upgrade completion
    handler_->OnUpgradeComplete(context);
    
    // State should be updated to UPGRADED
    EXPECT_EQ(context.state, ConnectionState::UPGRADED);
}

// Test upgrade failure
TEST_F(BaseSmartHandlerTest, UpgradeFailure) {
    handler_->HandleConnect(socket_);
    
    // Create a connection context
    ConnectionContext context(socket_);
    context.state = ConnectionState::NEGOTIATING;
    
    // Test upgrade failure
    handler_->OnUpgradeFailed(context, "Test error");
    
    // State should be updated to FAILED
    EXPECT_EQ(context.state, ConnectionState::FAILED);
}

// Test negotiation timeout
TEST_F(BaseSmartHandlerTest, NegotiationTimeout) {
    handler_->HandleConnect(socket_);
    
    // Simulate negotiation timeout
    handler_->HandleNegotiationTimeout(socket_);
    
    // Timeout handling should not crash
    // The actual timeout logic depends on the specific implementation
}

// Test write during negotiation
TEST_F(BaseSmartHandlerTest, WriteDuringNegotiation) {
    handler_->HandleConnect(socket_);
    
    // Create a connection context in NEGOTIATING state
    ConnectionContext context(socket_);
    context.state = ConnectionState::NEGOTIATING;
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n\r\n";
    context.pending_response = std::vector<uint8_t>(response.begin(), response.end());
    context.response_sent = 0;
    
    // Test response sending - this should call WriteData
    handler_->TrySendResponse(context);
    
    EXPECT_TRUE(handler_->IsWriteCalled());  // WriteData should be called during negotiation
}

// Test response sending
TEST_F(BaseSmartHandlerTest, ResponseSending) {
    handler_->HandleConnect(socket_);
    
    // Create a connection context with pending response
    ConnectionContext context(socket_);
    std::string response = "HTTP/1.1 101 Switching Protocols\r\n\r\n";
    context.pending_response = std::vector<uint8_t>(response.begin(), response.end());
    context.response_sent = 0;
    
    // Test response sending
    handler_->TrySendResponse(context);
    
    EXPECT_TRUE(handler_->IsWriteCalled());  // WriteData should be called
}

// Test multiple connections
TEST_F(BaseSmartHandlerTest, MultipleConnections) {
    auto socket1 = std::make_shared<TcpSocket>();
    auto socket2 = std::make_shared<TcpSocket>();
    
    // Handle multiple connections
    handler_->HandleConnect(socket1);
    handler_->HandleConnect(socket2);
    
    EXPECT_TRUE(handler_->IsInitCalled());
    
    // Handle operations on different sockets
    handler_->HandleRead(socket1);
    handler_->HandleWrite(socket2);  // This won't call WriteData unless in NEGOTIATING state
    
    EXPECT_TRUE(handler_->IsReadCalled());
    EXPECT_FALSE(handler_->IsWriteCalled());  // WriteData is only called during negotiation
}

// Test event driver integration
TEST_F(BaseSmartHandlerTest, EventDriverIntegration) {
    handler_->HandleConnect(socket_);
    
    // Event driver is set but not automatically initialized
    EXPECT_FALSE(event_driver_->IsInitCalled());  // SetEventDriver doesn't call Init()
    
    // Test response sending with event driver
    ConnectionContext context(socket_);
    std::string response = "HTTP/1.1 200 OK\r\n\r\n";
    context.pending_response = std::vector<uint8_t>(response.begin(), response.end());
    
    handler_->TrySendResponse(context);
    
    // Event driver should be used for modifying file descriptors
    auto modify_calls = event_driver_->GetModifyCalls();
    // The exact number of calls depends on the implementation
}

// Test handler lifecycle
TEST_F(BaseSmartHandlerTest, HandlerLifecycle) {
    // Create handler
    EXPECT_NE(handler_, nullptr);
    
    // Connect
    handler_->HandleConnect(socket_);
    EXPECT_TRUE(handler_->IsInitCalled());
    
    // Read data
    handler_->HandleRead(socket_);
    EXPECT_TRUE(handler_->IsReadCalled());
    
    // Write data - this won't call WriteData unless in NEGOTIATING state
    handler_->HandleWrite(socket_);
    EXPECT_FALSE(handler_->IsWriteCalled());  // WriteData is only called during negotiation
    
    // Close connection
    handler_->HandleClose(socket_);
    EXPECT_TRUE(handler_->IsCleanupCalled());
}

}
} // namespace upgrade
} // namespace quicx 