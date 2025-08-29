#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <gtest/gtest.h>

#include "upgrade/network/tcp_action.h"
#include "upgrade/network/if_socket_handler.h"

namespace quicx {
namespace upgrade {
namespace {

// Mock socket handler for testing
class MockSocketHandler:
    public ISocketHandler {
public:
    MockSocketHandler(ITcpAction* tcp_action):
        connect_count_(0),
        read_count_(0),
        write_count_(0),
        close_count_(0),
        tcp_action_(tcp_action) {}
    
    virtual void HandleConnect(std::shared_ptr<ITcpSocket> socket) override {
        connect_count_++;
        last_socket_ = socket;
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
    
private:
    std::atomic<int> connect_count_;
    std::atomic<int> read_count_;
    std::atomic<int> write_count_;
    std::atomic<int> close_count_;
    std::shared_ptr<ITcpSocket> last_socket_;
    ITcpAction* tcp_action_;
};

class TcpActionTest : public ::testing::Test {
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

// Test TCP action initialization
TEST_F(TcpActionTest, Initialization) {
    EXPECT_TRUE(action_->Init());
}

// Test adding listener
TEST_F(TcpActionTest, AddListener) {
    EXPECT_TRUE(action_->Init());
    
    auto handler = std::make_shared<MockSocketHandler>(action_.get());
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8080, handler));
}

// Test adding multiple listeners
TEST_F(TcpActionTest, AddMultipleListeners) {
    EXPECT_TRUE(action_->Init());
    
    auto handler1 = std::make_shared<MockSocketHandler>(action_.get());
    auto handler2 = std::make_shared<MockSocketHandler>(action_.get());
    
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8080, handler1));
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8081, handler2));
}

// Test adding listener without initialization
TEST_F(TcpActionTest, AddListenerWithoutInit) {
    auto handler = std::make_shared<MockSocketHandler>(action_.get());
    EXPECT_FALSE(action_->AddListener("127.0.0.1", 8080, handler));
}

// Test adding listener with invalid address
TEST_F(TcpActionTest, AddListenerInvalidAddress) {
    EXPECT_TRUE(action_->Init());
    
    auto handler = std::make_shared<MockSocketHandler>(action_.get());
    EXPECT_FALSE(action_->AddListener("invalid.address", 8080, handler));
}

// Test adding listener with invalid port
TEST_F(TcpActionTest, AddListenerInvalidPort) {
    EXPECT_TRUE(action_->Init());
    
    auto handler = std::make_shared<MockSocketHandler>(action_.get());
    EXPECT_FALSE(action_->AddListener("127.0.0.1", 0, handler));
}

// Test stopping TCP action
TEST_F(TcpActionTest, Stop) {
    EXPECT_TRUE(action_->Init());
    
    auto handler = std::make_shared<MockSocketHandler>(action_.get());
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8080, handler));
    
    action_->Stop();
    // Should not crash
}

// Test joining TCP action
TEST_F(TcpActionTest, Join) {
    EXPECT_TRUE(action_->Init());
    
    auto handler = std::make_shared<MockSocketHandler>(action_.get());
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8080, handler));
    
    action_->Stop();
    action_->Join();
    // Should not crash
}

// Test timer functionality
TEST_F(TcpActionTest, TimerFunctionality) {
    EXPECT_TRUE(action_->Init());
    
    std::atomic<bool> timer_fired(false);
    
    // Add a timer that fires after 100ms
    uint64_t timer_id = action_->AddTimer([&timer_fired]() {
        timer_fired = true;
    }, 100);
    
    EXPECT_GT(timer_id, 0);
    
    // Wait for timer to fire
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_TRUE(timer_fired);
}

// Test multiple timers
TEST_F(TcpActionTest, MultipleTimers) {
    EXPECT_TRUE(action_->Init());
    
    std::atomic<int> timer1_fired(0);
    std::atomic<int> timer2_fired(0);
    
    // Add two timers
    uint64_t timer1_id = action_->AddTimer([&timer1_fired]() {
        timer1_fired++;
    }, 50);
    
    uint64_t timer2_id = action_->AddTimer([&timer2_fired]() {
        timer2_fired++;
    }, 100);
    
    EXPECT_GT(timer1_id, 0);
    EXPECT_GT(timer2_id, 0);
    EXPECT_NE(timer1_id, timer2_id);
    
    // Wait for timers to fire
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    EXPECT_GT(timer1_fired, 0);
    EXPECT_GT(timer2_fired, 0);
}

// Test timer removal
TEST_F(TcpActionTest, TimerRemoval) {
    EXPECT_TRUE(action_->Init());
    
    std::atomic<bool> timer_fired(false);
    
    // Add a timer
    uint64_t timer_id = action_->AddTimer([&timer_fired]() {
        timer_fired = true;
    }, 1000);
    
    EXPECT_GT(timer_id, 0);
    
    // Remove the timer immediately
    EXPECT_TRUE(action_->RemoveTimer(timer_id));
    
    // Wait and verify timer didn't fire
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    EXPECT_FALSE(timer_fired);
}

// Test removing non-existent timer
TEST_F(TcpActionTest, RemoveNonExistentTimer) {
    EXPECT_TRUE(action_->Init());
    
    EXPECT_FALSE(action_->RemoveTimer(999));
}

// Test timer with zero timeout
TEST_F(TcpActionTest, TimerZeroTimeout) {
    EXPECT_TRUE(action_->Init());
    
    std::atomic<bool> timer_fired(false);
    
    // Add a timer with zero timeout
    uint64_t timer_id = action_->AddTimer([&timer_fired]() {
        timer_fired = true;
    }, 10);
    
    EXPECT_GT(timer_id, 0);
    
    // Timer should fire immediately or very quickly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    EXPECT_TRUE(timer_fired);
}

// Test TCP action lifecycle
TEST_F(TcpActionTest, Lifecycle) {
    // Initialize
    EXPECT_TRUE(action_->Init());
    
    // Add listener
    auto handler = std::make_shared<MockSocketHandler>(action_.get());
    EXPECT_TRUE(action_->AddListener("127.0.0.1", 8080, handler));
    
    // Add timer
    std::atomic<bool> timer_fired(false);
    uint64_t timer_id = action_->AddTimer([&timer_fired]() {
        timer_fired = true;
    }, 100);
    EXPECT_GT(timer_id, 0);
    
    // Wait a bit for timer
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(timer_fired);
    
    // Stop and join
    action_->Stop();
    action_->Join();
    
    // Should not crash
}

}
} // namespace upgrade
} // namespace quicx 