#include <gtest/gtest.h>
#include <quicx/common/if_timer_scheduler.h>

#include <memory>
#include <thread>
#include <utility>

#include "common/timer/timer_core.h"

namespace quicx {
namespace common {
namespace {

// Small helper mirroring what EventLoop::AddTimer will do in Task 12.
Timer Arm(std::shared_ptr<TimerCore> core, std::function<void()> cb, uint32_t delay_ms, uint64_t now) {
    uint32_t gen = 0;
    uint32_t index = core->Arm(std::move(cb), {}, false, delay_ms, 0, now, gen);
    return Timer(core, index, gen);
}

TEST(TimerHandleTest, DestructionCancels) {
    auto core = std::make_shared<TimerCore>();
    core->BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;
    int fired = 0;
    {
        Timer timer = Arm(core, [&fired]() { ++fired; }, 50, now);
        EXPECT_TRUE(timer.IsActive());
    }
    core->Run(now + 100);
    EXPECT_EQ(0, fired) << "~Timer must cancel the timer";
    EXPECT_TRUE(core->Empty());
}

TEST(TimerHandleTest, EmptyHandleIsInertAndSafeToCancel) {
    Timer timer;
    EXPECT_FALSE(timer.IsActive());
    timer.Cancel();
    EXPECT_FALSE(timer.Rearm(10, 1000000));
}

TEST(TimerHandleTest, MoveTransfersOwnership) {
    auto core = std::make_shared<TimerCore>();
    core->BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;
    int fired = 0;

    Timer a = Arm(core, [&fired]() { ++fired; }, 10, now);
    Timer b = std::move(a);
    EXPECT_FALSE(a.IsActive()) << "the moved-from handle must be empty";
    EXPECT_TRUE(b.IsActive());

    core->Run(now + 10);
    EXPECT_EQ(1, fired) << "moving must not cancel the timer";
}

TEST(TimerHandleTest, MoveAssignmentCancelsThePreviouslyOwnedTimer) {
    auto core = std::make_shared<TimerCore>();
    core->BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;
    int old_fired = 0;
    int new_fired = 0;

    Timer holder = Arm(core, [&old_fired]() { ++old_fired; }, 10, now);
    holder = Arm(core, [&new_fired]() { ++new_fired; }, 10, now);

    core->Run(now + 10);
    EXPECT_EQ(0, old_fired) << "the overwritten timer must have been cancelled";
    EXPECT_EQ(1, new_fired);
}

TEST(TimerHandleTest, CancelIsIdempotentAndDisablesRearm) {
    auto core = std::make_shared<TimerCore>();
    core->BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;
    int fired = 0;

    Timer timer = Arm(core, [&fired]() { ++fired; }, 50, now);
    timer.Cancel();
    timer.Cancel();
    EXPECT_FALSE(timer.IsActive());
    EXPECT_FALSE(timer.Rearm(10, now)) << "a cancelled handle must not be revivable";

    core->Run(now + 100);
    EXPECT_EQ(0, fired);
}

TEST(TimerHandleTest, RearmKeepsTheHandleUsable) {
    auto core = std::make_shared<TimerCore>();
    core->BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;
    int fired = 0;

    Timer timer = Arm(core, [&fired]() { ++fired; }, 50, now);
    ASSERT_TRUE(timer.Rearm(1000, now));
    EXPECT_TRUE(timer.IsActive());

    core->Run(now + 999);
    EXPECT_EQ(0, fired);
    core->Run(now + 1000);
    EXPECT_EQ(1, fired);
}

TEST(TimerHandleTest, DestructionFromAnotherThreadIsSafe) {
    auto core = std::make_shared<TimerCore>();
    core->BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;
    int fired = 0;

    auto timer = std::make_unique<Timer>(Arm(core, [&fired]() { ++fired; }, 50, now));
    // This is the case that used to require the hand-rolled
    // "IsInLoopThread() ? RemoveTimer : RunInLoop(remove by id)" dance at six
    // different call sites, and that got the idle timer's handle lost (the P0
    // use-after-free). With the handle it is just a destructor.
    std::thread([&timer]() { timer.reset(); }).join();

    core->Run(now + 100);
    EXPECT_EQ(0, fired) << "cross-thread destruction must still cancel";
    EXPECT_TRUE(core->Empty());
}

TEST(TimerHandleTest, HandleOutlivingClearDegradesToNoOp) {
    auto core = std::make_shared<TimerCore>();
    core->BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;

    Timer timer = Arm(core, []() {}, 50, now);
    core->Clear();
    EXPECT_FALSE(timer.IsActive());
    timer.Cancel();  // must not corrupt the recycled slot
    EXPECT_TRUE(core->Empty());
}

}  // namespace
}  // namespace common
}  // namespace quicx
