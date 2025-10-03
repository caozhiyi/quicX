#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <gtest/gtest.h>


#include "upgrade/network/tcp_socket.h"
#include "common/network/if_event_loop.h"
#include "common/network/if_event_driver.h"
#include "upgrade/include/if_upgrade.h"

namespace quicx {
namespace upgrade {
namespace {

class NetworkIntegrationTest:
    public ::testing::Test {
protected:
    void SetUp() override {
        event_loop_ = common::MakeEventLoop();
    }
    
    void TearDown() override {
        event_loop_.reset();
    }
    
    std::shared_ptr<common::IEventLoop> event_loop_;
};

// Test complete UpgradeServer + EventLoop basic lifecycle
TEST_F(NetworkIntegrationTest, CompleteUpgradeLifecycle) {
    ASSERT_TRUE(event_loop_->Init());
    auto server = IUpgrade::MakeUpgrade(event_loop_);
    ASSERT_NE(server, nullptr);

    UpgradeSettings settings; settings.listen_addr = "127.0.0.1"; settings.http_port = 8080; settings.https_port = 0;
    EXPECT_TRUE(server->AddListener(settings));

    std::atomic<bool> timer_fired(false);
    uint64_t timer_id = event_loop_->AddTimer([&timer_fired]() { timer_fired = true; }, 1);
    EXPECT_GT(timer_id, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    event_loop_->Wait();
    EXPECT_TRUE(timer_fired);
}

// Test TCP socket basics
TEST_F(NetworkIntegrationTest, TcpSocketBasics) {
    auto socket = std::make_unique<TcpSocket>();
    
    // Test socket validity
    EXPECT_TRUE(socket->IsValid());
    EXPECT_GE(socket->GetFd(), 0);
    
    // Close socket
    socket->Close();
    EXPECT_FALSE(socket->IsValid());
}

// Test event driver integration
TEST_F(NetworkIntegrationTest, EventDriverIntegration) {
    auto driver = common::IEventDriver::Create();
    EXPECT_NE(driver, nullptr);
    
    EXPECT_TRUE(driver->Init());
    
    // Test wakeup
    std::atomic<bool> wakeup_called(false);
    std::thread wait_thread([&driver, &wakeup_called]() {
        std::vector<common::Event> events;
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
    
    // Create multiple sockets
    for (int i = 0; i < 5; ++i) {
        auto socket = std::make_unique<TcpSocket>();
        EXPECT_TRUE(socket->IsValid());
        
        sockets.push_back(std::move(socket));
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

// Test multiple listeners via UpgradeServer and timers on event loop
TEST_F(NetworkIntegrationTest, MultipleListenersAndTimers) {
    ASSERT_TRUE(event_loop_->Init());
    auto server = IUpgrade::MakeUpgrade(event_loop_);
    ASSERT_NE(server, nullptr);

    UpgradeSettings s1; s1.listen_addr = "127.0.0.1"; s1.http_port = 8080; s1.https_port = 0;
    UpgradeSettings s2; s2.listen_addr = "127.0.0.1"; s2.http_port = 8081; s2.https_port = 0;

    EXPECT_TRUE(server->AddListener(s1));
    EXPECT_TRUE(server->AddListener(s2));

    std::atomic<int> t1(0), t2(0);
    uint64_t id1 = event_loop_->AddTimer([&t1]() { t1++; }, 5);
    uint64_t id2 = event_loop_->AddTimer([&t2]() { t2++; }, 10);
    EXPECT_GT(id1, 0); EXPECT_GT(id2, 0); EXPECT_NE(id1, id2);

    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    event_loop_->Wait();
    EXPECT_GT(t1, 0); EXPECT_GT(t2, 0);
}

// Test error handling basics
TEST_F(NetworkIntegrationTest, ErrorHandling) {
    // Invalid timer removal without init event loop
    if (event_loop_) {
        EXPECT_FALSE(event_loop_->RemoveTimer(999));
    }
    
    // Test TCP socket with invalid FD (create a socket and then close it)
    auto socket = std::make_unique<TcpSocket>();
    EXPECT_TRUE(socket->IsValid());  // Socket should be valid initially
    
    // Close the socket to make it invalid
    socket->Close();
    EXPECT_FALSE(socket->IsValid());  // Now it should be invalid
    
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    EXPECT_LT(socket->Send(data), 0);  // Send should fail on closed socket
}

}
} // namespace upgrade
} // namespace quicx 