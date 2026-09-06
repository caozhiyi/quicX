#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <quicx/upgrade/if_upgrade.h>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "common/network/if_event_driver.h"
#include "common/network/if_event_loop.h"

#include "upgrade/network/tcp_socket.h"

namespace quicx {
namespace upgrade {
namespace {

class NetworkIntegrationTest: public ::testing::Test {
protected:
    void SetUp() override { event_loop_ = common::MakeEventLoop(); }

    void TearDown() override { event_loop_.reset(); }

    std::shared_ptr<common::IEventLoop> event_loop_;
};

// Test complete UpgradeServer lifecycle: the server owns its own event loop
// and thread, so the caller only ever sees AddListener()/Stop().
TEST_F(NetworkIntegrationTest, CompleteUpgradeLifecycle) {
    auto server = IUpgrade::MakeUpgrade();
    ASSERT_NE(server, nullptr);

    UpgradeSettings settings;
    settings.listen_addr_ = "127.0.0.1";
    settings.http_port_ = 8080;
    settings.https_port_ = 0;
    EXPECT_TRUE(server->AddListener(settings));

    // Stop() is idempotent, and a stopped server refuses new listeners
    // (otherwise AddListener() would silently resurrect the loop thread).
    server->Stop();
    server->Stop();
    EXPECT_FALSE(server->AddListener(settings));
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
    EXPECT_EQ(it, fds.end());  // All FDs should be unique
}

// Test multiple listeners on one server: every AddListener() call is handed
// to the server's own loop thread, so both ports end up registered there.
TEST_F(NetworkIntegrationTest, MultipleListeners) {
    auto server = IUpgrade::MakeUpgrade();
    ASSERT_NE(server, nullptr);

    UpgradeSettings s1;
    s1.listen_addr_ = "127.0.0.1";
    s1.http_port_ = 8080;
    s1.https_port_ = 0;
    UpgradeSettings s2;
    s2.listen_addr_ = "127.0.0.1";
    s2.http_port_ = 8081;
    s2.https_port_ = 0;

    EXPECT_TRUE(server->AddListener(s1));
    EXPECT_TRUE(server->AddListener(s2));

    server->Stop();
}

// Test error handling basics
TEST_F(NetworkIntegrationTest, ErrorHandling) {
    // Cancelling requires an initialized event loop (Init() sets thread_id_).
    // Without Init(), AssertInLoopThread() aborts because thread_id_ is default.
    ASSERT_TRUE(event_loop_->Init());
    // Cancelling an empty handle, and cancelling twice, must be no-ops rather
    // than crashes. There is no longer an id to fabricate, which is the point:
    // "cancel timer 999" cannot be expressed at all.
    common::Timer empty;
    EXPECT_FALSE(empty.IsActive());
    empty.Cancel();
    empty.Cancel();

    // Test TCP socket with invalid FD (create a socket and then close it)
    auto socket = std::make_unique<TcpSocket>();
    EXPECT_TRUE(socket->IsValid());  // Socket should be valid initially

    // Close the socket to make it invalid
    socket->Close();
    EXPECT_FALSE(socket->IsValid());  // Now it should be invalid

    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    EXPECT_LT(socket->Send(data), 0);  // Send should fail on closed socket
}

}  // namespace
}  // namespace upgrade
}  // namespace quicx