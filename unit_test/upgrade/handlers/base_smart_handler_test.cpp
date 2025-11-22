#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <string>
#include <atomic>

#include "common/timer/timer_task.h"
#include "common/network/if_event_loop.h"

#include "upgrade/include/type.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/handlers/base_smart_handler.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {
namespace {

// Mock event loop for testing
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

// Concrete implementation of BaseSmartHandler for testing
class TestSmartHandler:
    public BaseSmartHandler {
public:
    explicit TestSmartHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop):
        BaseSmartHandler(settings, event_loop),
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
        
        event_loop_ = std::make_shared<MockEventLoop>();
        handler_ = std::make_unique<TestSmartHandler>(settings_, event_loop_);
        socket_ = std::make_shared<TcpSocket>();
    }
    
    void TearDown() override {
        handler_.reset();
        socket_.reset();
        event_loop_.reset();
    }
    
    UpgradeSettings settings_;
    std::unique_ptr<TestSmartHandler> handler_;
    std::shared_ptr<ITcpSocket> socket_;
    std::shared_ptr<MockEventLoop> event_loop_;
};

// Test handler creation
TEST_F(BaseSmartHandlerTest, HandlerCreation) {
    EXPECT_NE(handler_, nullptr);
    EXPECT_EQ(handler_->GetType(), "TEST");
}

// Test handle connect
TEST_F(BaseSmartHandlerTest, HandleConnect) {
    uint32_t fd = 100;
    handler_->OnConnect(fd);
    
    EXPECT_TRUE(handler_->IsInitCalled());
    
    // Check if connection context was created
    // Note: We can't directly access connections_ map, but we can verify
    // that the handler processed the connection
}

// Test handle read
TEST_F(BaseSmartHandlerTest, HandleRead) {
    // First connect
    uint32_t fd = 101;
    handler_->OnConnect(fd);
    
    // Then read
    handler_->OnRead(fd);
    
    EXPECT_TRUE(handler_->IsReadCalled());
}

// Test handle write
TEST_F(BaseSmartHandlerTest, HandleWrite) {
    // First connect
    uint32_t fd = 102;
    handler_->OnConnect(fd);
    
    // Then write - this won't call WriteData unless in NEGOTIATING state
    handler_->OnWrite(fd);
    
    EXPECT_FALSE(handler_->IsWriteCalled());  // WriteData is only called during negotiation
}

// Test handle close
TEST_F(BaseSmartHandlerTest, HandleClose) {
    // First connect
    uint32_t fd = 103;
    handler_->OnConnect(fd);
    
    // Then close
    handler_->OnClose(fd);
    
    EXPECT_TRUE(handler_->IsCleanupCalled());
}

// Test protocol detection
TEST_F(BaseSmartHandlerTest, ProtocolDetection) {
    uint32_t fd = 104;
    handler_->OnConnect(fd);
    
    // Simulate reading HTTP data
    std::vector<uint8_t> http_data = {0x48, 0x54, 0x54, 0x50, 0x2F, 0x31, 0x2E, 0x31}; // "HTTP/1.1"
    handler_->HandleProtocolDetection(fd, http_data);
    
    // Protocol detection should be triggered
    // Note: We can't directly verify the internal state, but we can ensure
    // the method doesn't crash and processes the data
}

// Test upgrade completion
TEST_F(BaseSmartHandlerTest, UpgradeCompletion) {
    handler_->OnConnect(105);
    
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
    handler_->OnConnect(106);
    
    // Simulate negotiation timeout
    handler_->HandleNegotiationTimeout(106);
    
    // Timeout handling should not crash
    // The actual timeout logic depends on the specific implementation
}

// Test write during negotiation
TEST_F(BaseSmartHandlerTest, WriteDuringNegotiation) {
    handler_->OnConnect(107);
    
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
    // Handle multiple connections
    handler_->OnConnect(108);
    handler_->OnConnect(109);
    
    EXPECT_TRUE(handler_->IsInitCalled());
    
    // Handle operations on different sockets
    handler_->OnRead(108);
    handler_->OnWrite(109);  // This won't call WriteData unless in NEGOTIATING state
    
    EXPECT_TRUE(handler_->IsReadCalled());
    EXPECT_FALSE(handler_->IsWriteCalled());  // WriteData is only called during negotiation
}

// Test event driver integration
TEST_F(BaseSmartHandlerTest, EventDriverIntegration) {
    handler_->OnConnect(110);
    
    // Event loop is set but not automatically initialized
    EXPECT_FALSE(event_loop_->IsInitCalled());  // Constructor doesn't call Init()
    
    // Test response sending with event driver
    ConnectionContext context(socket_);
    std::string response = "HTTP/1.1 200 OK\r\n\r\n";
    context.pending_response = std::vector<uint8_t>(response.begin(), response.end());
    
    handler_->TrySendResponse(context);
    
    // Event loop should be used for modifying file descriptors
    auto modify_calls = event_loop_->GetModifyCalls();
    // The exact number of calls depends on the implementation
}

// Test handler lifecycle
TEST_F(BaseSmartHandlerTest, HandlerLifecycle) {
    // Create handler
    EXPECT_NE(handler_, nullptr);
    
    // Connect
    handler_->OnConnect(111);
    EXPECT_TRUE(handler_->IsInitCalled());
    
    // Read data
    handler_->OnRead(111);
    EXPECT_TRUE(handler_->IsReadCalled());
    
    // Write data - this won't call WriteData unless in NEGOTIATING state
    handler_->OnWrite(111);
    EXPECT_FALSE(handler_->IsWriteCalled());  // WriteData is only called during negotiation
    
    // Close connection
    handler_->OnClose(111);
    EXPECT_TRUE(handler_->IsCleanupCalled());
}

}
} // namespace upgrade
} // namespace quicx 