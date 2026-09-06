#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

// EventLoop unit tests
#include <gtest/gtest.h>

#include "common/network/event_loop.h"
#include "common/network/if_event_driver.h"
#include "common/network/if_event_loop.h"
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
    auto owner = std::make_shared<int>(0);
    Timer timer = loop.AddTimer(owner, [&]() { fired++; }, 5 /*ms*/);
    EXPECT_TRUE(timer.IsActive());

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
    auto owner = std::make_shared<int>(0);
    Timer timer = loop.AddTimer(owner, [&]() { fired++; }, 20 /*ms*/);
    ASSERT_TRUE(timer.IsActive());
    timer.Cancel();
    EXPECT_FALSE(timer.IsActive());

    // Ensure Wait doesn't block long; no timer should fire
    RunLoopNTimes(loop, 2);
    EXPECT_EQ(fired.load(), 0);
}

TEST(EventLoopTest, RepeatTimerFiresPeriodicallyWithoutRearm) {
    EventLoop loop;
    ASSERT_TRUE(loop.Init());

    std::atomic<int> fired{0};
    auto owner = std::make_shared<int>(0);
    // A single schedule must keep firing every interval_ms until cancelled.
    // This is the primitive the http3 stream-cleanup timer switched to, so it
    // must repeat on its own -- the old one-shot + self-reschedule pattern is
    // gone and must not be needed here.
    Timer timer = loop.AddRepeatTimer(owner, [&]() { fired++; }, 10 /*ms*/);
    ASSERT_TRUE(timer.IsActive());

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (std::chrono::steady_clock::now() < deadline) {
        loop.Wakeup();
        loop.Wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    EXPECT_GE(fired.load(), 5) << "AddRepeatTimer must re-fire on its own; only " << fired.load() << " fires in ~200ms";

    timer.Cancel();
    int after_cancel = fired.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(after_cancel, fired.load()) << "repeat timer kept firing after Cancel()";
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

// ============================================================================
// Regression suite: pure-timer self-driving semantics.
//
// Production assumes that calling Wait() with no fd activity and no Wakeup()
// will still return when the next timer is due, and the timer's callback
// will have fired. The 9-second interop silence indicates this contract was
// violated. These tests assert it directly.
// ============================================================================

TEST(EventLoopTest, PureTimerSelfDrives_50ms) {
    EventLoop loop;
    ASSERT_TRUE(loop.Init());

    std::atomic<int> fired{0};
    auto t0 = std::chrono::steady_clock::now();
    auto owner = std::make_shared<int>(0);
    Timer timer = loop.AddTimer(owner, [&]() { fired++; }, 50 /*ms*/);
    ASSERT_TRUE(timer.IsActive());

    // Single Wait() — no Wakeup, no PostTask, no fd events.
    // Must return because the 50ms timer is due, and callback must have fired.
    loop.Wait();
    auto t1 = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    EXPECT_EQ(1, fired.load()) << "Timer did not fire after Wait() returned (elapsed=" << elapsed << "ms)";
    // Wait should have blocked for ~50ms (allow generous slack).
    EXPECT_GE(elapsed, 30) << "Wait() returned too early (timer not driving)";
    EXPECT_LE(elapsed, 500) << "Wait() blocked far longer than the 50ms timer";
}

TEST(EventLoopTest, PureTimerSelfDrives_LongTimeoutNotPrematurelyWoken) {
    EventLoop loop;
    ASSERT_TRUE(loop.Init());

    // Set a 9-second timer, then check Wait() doesn't return immediately
    // and that MinTime-driven blocking actually engages. We poll-bound
    // the test at ~150ms to keep the suite fast — the assertion is that
    // Wait() blocked >100ms (much more than spinning would).
    std::atomic<int> fired{0};
    auto owner = std::make_shared<int>(0);
    Timer timer = loop.AddTimer(owner, [&]() { fired++; }, 9000 /*ms*/);
    ASSERT_TRUE(timer.IsActive());

    auto t0 = std::chrono::steady_clock::now();

    // Wake the loop after 120ms so Wait() unblocks before the 9s timer fires.
    std::thread waker([&loop]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        loop.Wakeup();
    });

    loop.Wait();
    auto t1 = std::chrono::steady_clock::now();
    waker.join();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    // Should NOT have fired (woken via Wakeup, not the 9s timer).
    EXPECT_EQ(0, fired.load());
    // But also must NOT have spun: must have blocked >= ~100ms.
    EXPECT_GE(elapsed, 100) << "Wait() returned suspiciously fast (" << elapsed
                            << "ms) despite no events — possible busy-loop or 0-timeout poll";
    EXPECT_LE(elapsed, 1500);

    timer.Cancel();
    EXPECT_FALSE(timer.IsActive());
}

TEST(EventLoopTest, PureTimerHighFrequencyRearm) {
    // Reproduce the PTO usage pattern: every outgoing packet pushes the deadline
    // out, then the loop is driven. The timer must not fire while it is being
    // pushed out, and must fire once we stop.
    EventLoop loop;
    ASSERT_TRUE(loop.Init());

    std::atomic<int> fired{0};
    auto owner = std::make_shared<int>(0);
    Timer timer = loop.AddTimer(owner, [&]() { fired++; }, 30 /*ms*/);
    ASSERT_TRUE(timer.IsActive());

    // 200 rearm cycles at 30ms. Rearm reuses the node, which is the whole point
    // of the handle: the id-based predecessor had to remove and re-add, and a
    // dropped id there meant a timer that could never be cancelled again.
    for (int i = 0; i < 200; ++i) {
        ASSERT_TRUE(timer.Rearm(30));

        // Drive the loop briefly without sleeping long enough for the timer
        // to fire (so it gets re-armed every iteration).
        loop.Wakeup();
        loop.Wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(0, fired.load()) << "Timer fired during rearm loop (lost rearm)";

    // Stop re-arming; let it fire.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (fired.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        loop.Wait();
    }
    EXPECT_EQ(1, fired.load()) << "Timer never fired after rearm sequence";
}

}  // namespace
}  // namespace common
}  // namespace quicx
