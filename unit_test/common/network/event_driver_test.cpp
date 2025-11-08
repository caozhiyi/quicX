#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>

#include "common/network/io_handle.h"
#include "common/network/if_event_driver.h"

namespace quicx {
namespace common {
namespace {

static void SleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

TEST(EventDriverTest, CreateAndInit) {
    auto driver = IEventDriver::Create();
    ASSERT_NE(driver, nullptr);
    EXPECT_TRUE(driver->Init());
    EXPECT_GT(driver->GetMaxEvents(), 0);
}

TEST(EventDriverTest, AddReadFdAndReceiveEvent) {
    auto driver = IEventDriver::Create();
    ASSERT_NE(driver, nullptr);
    ASSERT_TRUE(driver->Init());

    int32_t rfd = -1, wfd = -1;
    ASSERT_TRUE(Pipe(rfd, wfd));

    ASSERT_TRUE(driver->AddFd(rfd, EventType::ET_READ));

    const char ch = 'a';
    auto wr = Write(wfd, &ch, 1);
    ASSERT_GE(wr.return_value_, 0);

    std::vector<Event> events;
    int n = driver->Wait(events, 50);
    EXPECT_GE(n, 1);

    bool got_read = false;
    for (const auto& ev : events) {
        if (ev.fd == rfd && ev.type == EventType::ET_READ) {
            got_read = true;
        }
    }
    EXPECT_TRUE(got_read);

    EXPECT_TRUE(driver->RemoveFd(rfd));
    Close(rfd);
    Close(wfd);
}

TEST(EventDriverTest, AddWriteFdAndReceiveEvent) {
    auto driver = IEventDriver::Create();
    ASSERT_NE(driver, nullptr);
    ASSERT_TRUE(driver->Init());

    int32_t rfd = -1, wfd = -1;
    ASSERT_TRUE(Pipe(rfd, wfd));

    ASSERT_TRUE(driver->AddFd(wfd, EventType::ET_WRITE));

    std::vector<Event> events;
    // Try a few times in case of scheduling hiccups
    bool got_write = false;
    for (int i = 0; i < 5 && !got_write; ++i) {
        int n = driver->Wait(events, 50);
        EXPECT_GE(n, 0);
        for (const auto& ev : events) {
            if (ev.fd == wfd && ev.type == EventType::ET_WRITE) {
                got_write = true;
                break;
            }
        }
    }
    EXPECT_TRUE(got_write);

    EXPECT_TRUE(driver->RemoveFd(wfd));
    Close(rfd);
    Close(wfd);
}

TEST(EventDriverTest, ModifyFdAndRemoveFd) {
    auto driver = IEventDriver::Create();
    ASSERT_NE(driver, nullptr);
    ASSERT_TRUE(driver->Init());

    int32_t rfd = -1, wfd = -1;
    ASSERT_TRUE(Pipe(rfd, wfd));

    ASSERT_TRUE(driver->AddFd(rfd, EventType::ET_READ));
    EXPECT_TRUE(driver->ModifyFd(rfd, EventType::ET_READ));

    // Generate a read event
    const char ch = 'b';
    auto wr = Write(wfd, &ch, 1);
    ASSERT_GE(wr.return_value_, 0);
    std::vector<Event> events;
    int n = driver->Wait(events, 50);
    EXPECT_GE(n, 1);

    EXPECT_TRUE(driver->RemoveFd(rfd));

    // After removal, new events for rfd should not show up
    auto wr2 = Write(wfd, &ch, 1);
    ASSERT_GE(wr2.return_value_, 0);
    events.clear();
    n = driver->Wait(events, 50);
    bool has_rfd = false;
    for (const auto& ev : events) {
        if (ev.fd == rfd) { has_rfd = true; }
    }
    EXPECT_FALSE(has_rfd);

    Close(rfd);
    Close(wfd);
}

TEST(EventDriverTest, WakeupUnblocksWait) {
    auto driver = IEventDriver::Create();
    ASSERT_NE(driver, nullptr);
    ASSERT_TRUE(driver->Init());

    std::vector<Event> events;

    std::thread t([&](){
        SleepMs(5);
        driver->Wakeup();
    });

    auto start = std::chrono::steady_clock::now();
    int n = driver->Wait(events, 1000);
    auto end = std::chrono::steady_clock::now();
    (void)n;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    t.join();
    EXPECT_LT(elapsed, 100);
}

} // namespace
} // namespace common
} // namespace quicx


