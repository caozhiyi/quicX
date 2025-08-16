#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include "common/network/io_handle.h"
#include "upgrade/network/if_event_driver.h"

namespace quicx {
namespace upgrade {
namespace {

class EventDriverTest : public ::testing::Test {
protected:
    void SetUp() override {
        driver_ = IEventDriver::Create();
        ASSERT_NE(driver_, nullptr);
    }
    
    void TearDown() override {
        driver_.reset();
    }
    
    std::unique_ptr<IEventDriver> driver_;
};

// Test event driver creation
TEST_F(EventDriverTest, Creation) {
    auto driver = IEventDriver::Create();
    EXPECT_NE(driver, nullptr);
}

// Test event driver initialization
TEST_F(EventDriverTest, Initialization) {
    EXPECT_TRUE(driver_->Init());
}

// Test adding file descriptor
TEST_F(EventDriverTest, AddFd) {
    EXPECT_TRUE(driver_->Init());
    
    // Create a pipe for testing
    uint64_t pipe_fds[2];
    ASSERT_TRUE(common::Pipe(pipe_fds[0], pipe_fds[1]));
    
    EXPECT_TRUE(driver_->AddFd(pipe_fds[0], EventType::ET_READ));
    
    // Clean up
    common::Close(pipe_fds[0]);
    common::Close(pipe_fds[1]);
}

// Test removing file descriptor
TEST_F(EventDriverTest, RemoveFd) {
    EXPECT_TRUE(driver_->Init());
    
    // Create a pipe for testing
    uint64_t pipe_fds[2];
    ASSERT_TRUE(common::Pipe(pipe_fds[0], pipe_fds[1]));
    
    EXPECT_TRUE(driver_->AddFd(pipe_fds[0], EventType::ET_READ));
    EXPECT_TRUE(driver_->RemoveFd(pipe_fds[0]));
    
    // Clean up
    common::Close(pipe_fds[0]);
    common::Close(pipe_fds[1]);
}

// Test waiting for events with timeout
TEST_F(EventDriverTest, WaitWithTimeout) {
    EXPECT_TRUE(driver_->Init());
    
    std::vector<Event> events;
    int result = driver_->Wait(events, 100); // 100ms timeout
    
    EXPECT_GE(result, 0); // Should not fail
    EXPECT_EQ(events.size(), 0); // No events should occur
}

// Test get max events
TEST_F(EventDriverTest, GetMaxEvents) {
    EXPECT_TRUE(driver_->Init());
    
    int max_events = driver_->GetMaxEvents();
    EXPECT_GT(max_events, 0);
}

// Test wakeup functionality
TEST_F(EventDriverTest, Wakeup) {
    EXPECT_TRUE(driver_->Init());
    
    std::atomic<bool> wakeup_called(false);
    std::atomic<bool> thread_started(false);
    
    // Start a thread that waits for events
    std::thread wait_thread([this, &wakeup_called, &thread_started]() {
        thread_started = true;
        std::vector<Event> events;
        driver_->Wait(events, 1000); // 1 second timeout
        wakeup_called = true;
    });
    
    // Wait for thread to start and begin waiting
    while (!thread_started) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Give the thread a moment to actually start waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Now wake up
    driver_->Wakeup();
    
    // Wait for thread to finish
    wait_thread.join();
    
    EXPECT_TRUE(wakeup_called);
}

}
} // namespace upgrade
} // namespace quicx 