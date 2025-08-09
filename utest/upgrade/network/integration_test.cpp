#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <algorithm>
#include <string>
#include <gtest/gtest.h>
#include <unistd.h>

#include "upgrade/network/tcp_action.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/network/if_event_driver.h"
#include "upgrade/network/if_socket_handler.h"

namespace quicx {
namespace upgrade {

// Mock socket handler for integration testing
class MockSocketHandler : public ISocketHandler {
public:
    MockSocketHandler() : connect_count_(0), read_count_(0), write_count_(0), close_count_(0) {}
    
    virtual void HandleConnect(std::shared_ptr<ITcpSocket> socket, std::shared_ptr<ITcpAction> action) override {
        connect_count_++;
        last_socket_ = socket;
        last_action_ = action;
    }
    
    virtual void HandleRead(std::shared_ptr<ITcpSocket> socket) override {
        read_count_++;
        last_socket_ = socket;
    }
    
    virtual void HandleWrite(std::shared_ptr<ITcpSocket> socket) override {
        write_count_++;
        last_socket_ = socket;
    }
    
    virtual void HandleClose(std::shared_ptr<ITcpSocket> socket) override {
        close_count_++;
        last_socket_ = socket;
    }
    
    // Test helper methods
    int GetConnectCount() const { return connect_count_; }
    int GetReadCount() const { return read_count_; }
    int GetWriteCount() const { return write_count_; }
    int GetCloseCount() const { return close_count_; }
    std::shared_ptr<ITcpSocket> GetLastSocket() const { return last_socket_; }
    std::shared_ptr<ITcpAction> GetLastAction() const { return last_action_; }
    
private:
    std::atomic<int> connect_count_;
    std::atomic<int> read_count_;
    std::atomic<int> write_count_;
    std::atomic<int> close_count_;
    std::shared_ptr<ITcpSocket> last_socket_;
    std::shared_ptr<ITcpAction> last_action_;
};

class NetworkIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        action_ = std::make_unique<TcpAction>();
    }
    
    void TearDown() override {
        if (action_) {
            action_->Stop();
            action_->Join();
        }
    }
    
    std::unique_ptr<TcpAction> action_;
};

// Test complete TCP action lifecycle
TEST_F(NetworkIntegrationTest, CompleteTcpActionLifecycle) {
    // Initialize
    EXPECT_TRUE(action_->Init());
    
    // Add listener
    auto handler = std::make_shared<MockSocketHandler>();
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8080, handler));
    
    // Add timer
    std::atomic<bool> timer_fired(false);
    uint64_t timer_id = action_->AddTimer([&timer_fired]() {
        timer_fired = true;
    }, 1);
    EXPECT_GT(timer_id, 0);
    
    // Wait for timer (give event loop ample time)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(timer_fired);
    
    // Stop and join
    action_->Stop();
    action_->Join();
}

// Test TCP socket with handler
TEST_F(NetworkIntegrationTest, TcpSocketWithHandler) {
    auto socket = std::make_unique<TcpSocket>();
    auto handler = std::make_shared<MockSocketHandler>();
    
    // Set handler
    socket->SetHandler(handler);
    EXPECT_EQ(socket->GetHandler(), handler);
    
    // Test socket validity
    EXPECT_TRUE(socket->IsValid());
    EXPECT_GT(socket->GetFd(), 0);
    
    // Test socket options
    EXPECT_TRUE(socket->SetNonBlocking(true));
    EXPECT_TRUE(socket->SetReuseAddr(true));
    EXPECT_TRUE(socket->SetKeepAlive(true));
    
    // Close socket
    socket->Close();
    EXPECT_FALSE(socket->IsValid());
}

// Test event driver integration
TEST_F(NetworkIntegrationTest, EventDriverIntegration) {
    auto driver = IEventDriver::Create();
    EXPECT_NE(driver, nullptr);
    
    EXPECT_TRUE(driver->Init());
    
    // Test wakeup
    std::atomic<bool> wakeup_called(false);
    std::thread wait_thread([&driver, &wakeup_called]() {
        std::vector<Event> events;
        driver->Wait(events, 1000);
        wakeup_called = true;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    driver->Wakeup();
    
    wait_thread.join();
    EXPECT_TRUE(wakeup_called);
    
    // No FDs to clean up since we relied on Wakeup-only
}

// Test multiple TCP sockets
TEST_F(NetworkIntegrationTest, MultipleTcpSockets) {
    std::vector<std::unique_ptr<TcpSocket>> sockets;
    std::vector<std::shared_ptr<MockSocketHandler>> handlers;
    
    // Create multiple sockets
    for (int i = 0; i < 5; ++i) {
        auto socket = std::make_unique<TcpSocket>();
        auto handler = std::make_shared<MockSocketHandler>();
        
        socket->SetHandler(handler);
        EXPECT_EQ(socket->GetHandler(), handler);
        EXPECT_TRUE(socket->IsValid());
        
        sockets.push_back(std::move(socket));
        handlers.push_back(handler);
    }
    
    // Verify all sockets are valid and have different FDs
    std::vector<int> fds;
    for (const auto& socket : sockets) {
        fds.push_back(socket->GetFd());
    }
    
    std::sort(fds.begin(), fds.end());
    auto it = std::unique(fds.begin(), fds.end());
    EXPECT_EQ(it, fds.end()); // All FDs should be unique
}

// Test TCP action with multiple listeners
TEST_F(NetworkIntegrationTest, MultipleListeners) {
    EXPECT_TRUE(action_->Init());
    
    auto handler1 = std::make_shared<MockSocketHandler>();
    auto handler2 = std::make_shared<MockSocketHandler>();
    
    // Add multiple listeners
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8080, handler1));
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8081, handler2));
    
    // Add timers
    std::atomic<int> timer1_count(0);
    std::atomic<int> timer2_count(0);
    
    uint64_t timer1_id = action_->AddTimer([&timer1_count]() {
        timer1_count++;
    }, 50);
    
    uint64_t timer2_id = action_->AddTimer([&timer2_count]() {
        timer2_count++;
    }, 100);
    
    EXPECT_GT(timer1_id, 0);
    EXPECT_GT(timer2_id, 0);
    EXPECT_NE(timer1_id, timer2_id);
    
    // Wait for timers (give event loop ample time)
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    EXPECT_GT(timer1_count, 0);
    EXPECT_GT(timer2_count, 0);
    
    action_->Stop();
    action_->Join();
}

// Test error handling
TEST_F(NetworkIntegrationTest, ErrorHandling) {
    // Test TCP action without initialization
    auto handler = std::make_shared<MockSocketHandler>();
    EXPECT_FALSE(action_->AddListener("127.0.0.1", 8080, handler));
    
    // Test invalid timer removal
    EXPECT_FALSE(action_->RemoveTimer(999));
    
    // Test TCP socket with invalid FD
    auto invalid_socket = std::make_unique<TcpSocket>(-1);
    EXPECT_FALSE(invalid_socket->IsValid());
    
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    EXPECT_LT(invalid_socket->Send(data), 0);
}

} // namespace upgrade
} // namespace quicx 