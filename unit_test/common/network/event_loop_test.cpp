// EventLoop unit tests
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

#include "common/network/event_loop.h"
#include "common/network/if_event_loop.h"
#include "common/network/if_event_driver.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace common {
namespace {

class TestHandler: public IFdHandler {
public:
    std::atomic<int> read_called{0};
    std::atomic<int> write_called{0};
    std::atomic<int> error_called{0};
    std::atomic<int> close_called{0};

    void OnRead(uint32_t) override { read_called++; }
    void OnWrite(uint32_t) override { write_called++; }
    void OnError(uint32_t) override { error_called++; }
    void OnClose(uint32_t) override { close_called++; }
};

// Helper to run Wait() a few times with small sleeps, to allow timers/tasks to trigger
static void RunLoopNTimes(IEventLoop& loop, int times) {
    for (int i = 0; i < times; ++i) {
        // Proactively wakeup to avoid default 1000ms timeout when no timers/events
        loop.Wakeup();
        loop.Wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

TEST(EventLoopTest, Init) {
    EventLoop loop;
    EXPECT_TRUE(loop.Init());
}

TEST(EventLoopTest, PostTaskExecutes) {
    EventLoop loop;
    ASSERT_TRUE(loop.Init());

    std::atomic<int> ran{0};
    loop.PostTask([&]() { ran++; });

    RunLoopNTimes(loop, 2);
    EXPECT_EQ(ran.load(), 1);
}

TEST(EventLoopTest, TimerFiresOnce) {
    EventLoop loop;
    ASSERT_TRUE(loop.Init());

    std::atomic<int> fired{0};
    uint64_t id = loop.AddTimer([&]() { fired++; }, 5 /*ms*/, false);
    EXPECT_GT(id, 0u);

    // Run until it should have fired
    for (int i = 0; i < 10 && fired.load() == 0; ++i) {
        loop.Wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_EQ(fired.load(), 1);
}

TEST(EventLoopTest, TimerCanBeRemoved) {
    EventLoop loop;
    ASSERT_TRUE(loop.Init());
    std::atomic<int> fired{0};
    uint64_t id = loop.AddTimer([&]() { fired++; }, 20 /*ms*/, false);
    ASSERT_GT(id, 0u);
    EXPECT_TRUE(loop.RemoveTimer(id));

    // Ensure Wait doesn't block long; no timer should fire
    RunLoopNTimes(loop, 2);
    EXPECT_EQ(fired.load(), 0);
}

TEST(EventLoopTest, WakeupUnblocksWait) {
    EventLoop loop;
    ASSERT_TRUE(loop.Init());

    // In another thread, wake up the loop shortly
    std::thread t([&loop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        loop.Wakeup();
    });

    auto start = std::chrono::steady_clock::now();
    int n = loop.Wait();
    auto end = std::chrono::steady_clock::now();
    (void)n;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    t.join();

    // If wakeup worked, Wait should have returned within ~50ms rather than default 1000ms
    EXPECT_LT(elapsed, 100);
}

TEST(EventLoopTest, RegisterModifyRemoveFdWithPipeAndDispatchRead) {
    EventLoop loop;
    ASSERT_TRUE(loop.Init());

    int32_t rfd = -1, wfd = -1;
    ASSERT_TRUE(Pipe(rfd, wfd));
    auto handler = std::make_shared<TestHandler>();

    ASSERT_TRUE(loop.RegisterFd(static_cast<uint32_t>(rfd), EventType::ET_READ, handler));
    ASSERT_TRUE(loop.ModifyFd(static_cast<uint32_t>(rfd), EventType::ET_READ));

    // Write data to trigger read readiness
    const char* msg = "x";
    auto wr = Write(wfd, msg, 1);
    ASSERT_GE(wr.return_value_, 0);

    // Wait for event and dispatch
    loop.Wait();
    EXPECT_GE(handler->read_called.load(), 1);

    EXPECT_TRUE(loop.RemoveFd(static_cast<uint32_t>(rfd)));

    // Cleanup
    Close(rfd);
    Close(wfd);
}

}  // namespace
}  // namespace common
}  // namespace quicx
