#include <functional>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/timer/timer_core.h"

namespace quicx {
namespace common {
namespace {

// ---------------------------------------------------------------------------
// Task 1: basic one-shot contract
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, ArmAndFireOneShot) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    uint32_t gen = 0;
    uint32_t idx = core.Arm([&fired]() { ++fired; }, {}, false, 50, 0, now, gen);

    ASSERT_NE(idx, TimerCore::kNoEntry);
    EXPECT_TRUE(core.IsActive(idx, gen));
    EXPECT_EQ(50, core.MinTime(now));

    core.Run(now + 49);
    EXPECT_EQ(0, fired) << "timer must not fire before its deadline";

    core.Run(now + 50);
    EXPECT_EQ(1, fired) << "timer must fire exactly at its deadline";
    EXPECT_FALSE(core.IsActive(idx, gen)) << "a fired one-shot must no longer be active";
    EXPECT_EQ(-1, core.MinTime(now + 50)) << "no timer left, MinTime must report -1";
}

// ---------------------------------------------------------------------------
// Task 2: handle resolution, cancel, idempotence, stale-handle safety
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, CancelLocalStopsCallbackAndIsIdempotent) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    uint32_t gen = 0;
    uint32_t idx = core.Arm([&fired]() { ++fired; }, {}, false, 50, 0, now, gen);

    EXPECT_TRUE(core.CancelLocal(idx, gen));
    EXPECT_FALSE(core.IsActive(idx, gen));
    EXPECT_FALSE(core.CancelLocal(idx, gen)) << "second cancel must be a no-op, not a double free";

    core.Run(now + 100);
    EXPECT_EQ(0, fired) << "a cancelled timer must never fire";
    EXPECT_TRUE(core.Empty());
}

TEST(TimerCoreTest, CancelReleasesClosureImmediately) {
    TimerCore core;
    auto probe = std::make_shared<int>(1);
    std::weak_ptr<int> weak = probe;
    uint64_t now = 1000000;
    uint32_t gen = 0;
    uint32_t idx = core.Arm([probe]() { (void)*probe; }, {}, false, 10000, 0, now, gen);
    probe.reset();
    ASSERT_FALSE(weak.expired());

    core.CancelLocal(idx, gen);
    EXPECT_TRUE(weak.expired()) << "cancel on the loop thread must drop the closure right away";
}

TEST(TimerCoreTest, StaleGenerationIsRejected) {
    TimerCore core;
    uint64_t now = 1000000;
    uint32_t gen1 = 0;
    uint32_t idx = core.Arm([]() {}, {}, false, 50, 0, now, gen1);
    core.CancelLocal(idx, gen1);
    core.ReleaseEntry(idx, gen1);

    uint32_t gen2 = 0;
    uint32_t idx2 = core.Arm([]() {}, {}, false, 70, 0, now, gen2);
    ASSERT_EQ(idx, idx2) << "the freed slab slot should be reused";
    EXPECT_NE(gen1, gen2);
    EXPECT_FALSE(core.CancelLocal(idx2, gen1)) << "a stale handle must not cancel the new timer";
    EXPECT_TRUE(core.IsActive(idx2, gen2));
}

// ---------------------------------------------------------------------------
// Task 3: in-place rearm
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, RearmMovesNodeAcrossLevelsKeepingHandleValid) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    uint32_t gen = 0;
    uint32_t idx = core.Arm([&fired]() { ++fired; }, {}, false, 50, 0, now, gen);
    EXPECT_EQ(50, core.MinTime(now));

    // L0 (50 ms) -> L1 (1000 ms)
    ASSERT_TRUE(core.Rearm(idx, gen, 1000, now));
    EXPECT_EQ(1000, core.MinTime(now));
    EXPECT_TRUE(core.IsActive(idx, gen)) << "Rearm must not invalidate the handle";

    core.Run(now + 999);
    EXPECT_EQ(0, fired) << "the old 50 ms deadline must be gone";
    core.Run(now + 1000);
    EXPECT_EQ(1, fired);
}

TEST(TimerCoreTest, RearmAfterFireReusesTheSameEntry) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    uint32_t gen = 0;
    uint32_t idx = core.Arm([&fired]() { ++fired; }, {}, false, 10, 0, now, gen);
    core.Run(now + 10);
    ASSERT_EQ(1, fired);
    ASSERT_FALSE(core.IsActive(idx, gen));

    ASSERT_TRUE(core.Rearm(idx, gen, 20, now + 10)) << "the entry must survive the fire so the handle stays usable";
    core.Run(now + 30);
    EXPECT_EQ(2, fired);
}

// ---------------------------------------------------------------------------
// Task 4: cross-thread cancel
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, RemoteCancelPreventsCallback) {
    TimerCore core;
    core.BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;
    int fired = 0;
    uint32_t gen = 0;
    uint32_t idx = core.Arm([&fired]() { ++fired; }, {}, false, 50, 0, now, gen);

    std::thread([&core, idx, gen]() { core.CancelRemote(idx, gen); }).join();

    core.Run(now + 100);
    EXPECT_EQ(0, fired) << "a remotely cancelled timer must not fire";
    EXPECT_TRUE(core.Empty()) << "Run() must drain the remote queue and release the node";
}

TEST(TimerCoreTest, RemoteCancelReleasesClosureAtNextRun) {
    TimerCore core;
    core.BindLoopThread(std::this_thread::get_id());
    auto probe = std::make_shared<int>(7);
    std::weak_ptr<int> weak = probe;
    uint64_t now = 1000000;
    uint32_t gen = 0;
    // The closure holds a strong reference, mimicking [self = shared_from_this()].
    uint32_t idx = core.Arm([probe]() { (void)*probe; }, {}, false, 10000, 0, now, gen);
    probe.reset();
    ASSERT_FALSE(weak.expired());

    std::thread([&core, idx, gen]() { core.CancelRemote(idx, gen); }).join();
    core.Run(now + 1);
    EXPECT_TRUE(weak.expired()) << "the closure must be released at the next Run(), not at the deadline";
}

TEST(TimerCoreTest, RemoteCancelArrivingDuringTickStillSuppressesLaterCallbacks) {
    TimerCore core;
    core.BindLoopThread(std::this_thread::get_id());
    uint64_t now = 1000000;
    int first = 0;
    int second = 0;
    uint32_t gen_second = 0;
    uint32_t idx_second = 0;

    // Both timers land on the same millisecond. The first one's callback issues a
    // remote cancel for the second, which must be honoured before it is invoked.
    uint32_t gen_first = 0;
    core.Arm(
        [&]() {
            ++first;
            std::thread([&core, idx_second, gen_second]() { core.CancelRemote(idx_second, gen_second); }).join();
        },
        {}, false, 10, 0, now, gen_first);
    idx_second = core.Arm([&second]() { ++second; }, {}, false, 10, 0, now, gen_second);

    core.Run(now + 10);
    EXPECT_EQ(1, first);
    EXPECT_EQ(0, second) << "a remote cancel issued mid-tick must suppress a not-yet-invoked callback";
}

// ---------------------------------------------------------------------------
// Task 5: owner guard
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, ExpiredOwnerSkipsCallback) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    auto owner = std::make_shared<int>(1);
    uint32_t gen = 0;
    core.Arm([&fired]() { ++fired; }, std::weak_ptr<void>(owner), true, 10, 0, now, gen);

    owner.reset();
    core.Run(now + 10);
    EXPECT_EQ(0, fired) << "the callback must be skipped once its owner is gone";
    EXPECT_TRUE(core.Empty());
}

TEST(TimerCoreTest, LiveOwnerRunsCallback) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    auto owner = std::make_shared<int>(1);
    uint32_t gen = 0;
    core.Arm([&fired]() { ++fired; }, std::weak_ptr<void>(owner), true, 10, 0, now, gen);

    core.Run(now + 10);
    EXPECT_EQ(1, fired);
}

// ---------------------------------------------------------------------------
// Task 6: repeat timers actually repeat
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, RepeatTimerFiresRepeatedly) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    uint32_t gen = 0;
    uint32_t idx = core.Arm([&fired]() { ++fired; }, {}, false, 100, /*interval_ms=*/100, now, gen);

    for (int i = 1; i <= 5; ++i) {
        core.Run(now + 100 * i);
        EXPECT_EQ(i, fired) << "a repeat timer must re-arm itself, iteration " << i;
    }

    EXPECT_TRUE(core.CancelLocal(idx, gen));
    core.Run(now + 100 * 10);
    EXPECT_EQ(5, fired) << "cancel must stop the repetition";
}

TEST(TimerCoreTest, RepeatCancelledInsideItsOwnCallback) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    auto self = std::make_shared<std::pair<uint32_t, uint32_t>>(TimerCore::kNoEntry, 0u);
    uint32_t gen = 0;
    uint32_t idx = core.Arm(
        [&core, &fired, self]() {
            ++fired;
            core.CancelLocal(self->first, self->second);
        },
        {}, false, 10, 10, now, gen);
    self->first = idx;
    self->second = gen;

    core.Run(now + 100);
    EXPECT_EQ(1, fired) << "self-cancel inside the callback must not be undone by the re-arm";
}

// ---------------------------------------------------------------------------
// Task 7: MinTime stays exact
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, MinTimeStaysExactUnderChurn) {
    TimerCore core;
    uint64_t now = 1000000;
    uint32_t g_long = 0;
    uint32_t g_short = 0;
    core.Arm([]() {}, {}, false, 10000, 0, now, g_long);
    uint32_t idx = core.Arm([]() {}, {}, false, 88, 0, now, g_short);

    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(core.Rearm(idx, g_short, 88, now));
        ASSERT_EQ(88, core.MinTime(now)) << "MinTime drifted after " << i << " rearm cycles";
    }
}

TEST(TimerCoreTest, MinTimeRecomputesAfterTheMinimumIsRemoved) {
    TimerCore core;
    uint64_t now = 1000000;
    uint32_t g1 = 0;
    uint32_t g2 = 0;
    uint32_t g3 = 0;
    uint32_t i1 = core.Arm([]() {}, {}, false, 10, 0, now, g1);
    uint32_t i2 = core.Arm([]() {}, {}, false, 500, 0, now, g2);
    core.Arm([]() {}, {}, false, 50000, 0, now, g3);

    EXPECT_EQ(10, core.MinTime(now));
    core.CancelLocal(i1, g1);
    EXPECT_EQ(500, core.MinTime(now));
    core.CancelLocal(i2, g2);
    EXPECT_EQ(50000, core.MinTime(now));
}

TEST(TimerCoreTest, MinTimeCountsMultipleHoldersOfTheSameDeadline) {
    TimerCore core;
    uint64_t now = 1000000;
    uint32_t ga = 0;
    uint32_t gb = 0;
    uint32_t ia = core.Arm([]() {}, {}, false, 30, 0, now, ga);
    core.Arm([]() {}, {}, false, 30, 0, now, gb);

    EXPECT_EQ(30, core.MinTime(now));
    core.CancelLocal(ia, ga);
    EXPECT_EQ(30, core.MinTime(now)) << "the other holder of the same deadline still pins the minimum";
}

// ---------------------------------------------------------------------------
// Task 8 / 9: cascade correctness and cheap idle advance
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, CascadeAcrossL1EpochPreservesAllTimers) {
    TimerCore core;
    uint64_t base = 262144;  // L1-epoch aligned
    int fired = 0;
    std::vector<uint32_t> gens(500, 0);
    for (int i = 0; i < 500; ++i) {
        core.Arm([&fired]() { ++fired; }, {}, false, static_cast<uint32_t>(300 + i), 0, base, gens[i]);
    }

    core.Run(base + 1000);
    EXPECT_EQ(500, fired) << "cascade must neither lose nor duplicate timers";
    EXPECT_TRUE(core.Empty());
}

TEST(TimerCoreTest, IdleAdvanceIsCorrectAcrossManyEmptyMilliseconds) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    uint32_t gen = 0;
    core.Arm([&fired]() { ++fired; }, {}, false, 60000, 0, now, gen);

    core.Run(now + 59999);
    EXPECT_EQ(0, fired);
    core.Run(now + 60000);
    EXPECT_EQ(1, fired);
}

// Regression: a timer further away than one full L2 revolution is still an
// overflow timer *after* the overflow list is cascaded. Draining the source list
// in place therefore re-inserted the node into the very list being drained and
// spun forever. Reproduced by TimingWheelTimerTest.long_timeout_overflow (a
// 2-hour timer), minimised here.
TEST(TimerCoreTest, OverflowCascadeTerminatesWhenTimerIsStillBeyondL2) {
    TimerCore core;
    uint64_t base = TimerCore::kL2Range * 4;  // L2-epoch aligned: c0 == c1 == c2 == 0
    int fired = 0;
    uint32_t gen = 0;
    uint32_t idx =
        core.Arm([&fired]() { ++fired; }, {}, false, static_cast<uint32_t>(TimerCore::kL2Range * 2), 0, base, gen);

    core.Run(base + 1000);  // crosses the L2 boundary -> Cascade(3, 0)

    EXPECT_EQ(0, fired) << "the timer is still far in the future";
    EXPECT_TRUE(core.IsActive(idx, gen)) << "it must survive the overflow cascade";
}

TEST(TimerCoreTest, DetachedTimerFiresAndNeedsNoHandle) {
    TimerCore core;
    uint64_t now = 1000000;
    int fired = 0;
    core.ArmDetached([&fired]() { ++fired; }, 25, now);

    EXPECT_EQ(25, core.MinTime(now));
    core.Run(now + 25);
    EXPECT_EQ(1, fired);
    EXPECT_TRUE(core.Empty());
}

// ---------------------------------------------------------------------------
// Task 10: Clear
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, ClearReleasesAllClosures) {
    TimerCore core;
    auto probe = std::make_shared<int>(1);
    std::weak_ptr<int> weak = probe;
    uint64_t now = 1000000;
    uint32_t gen = 0;
    core.Arm([probe]() {}, {}, false, 10000, 0, now, gen);
    core.ArmDetached([probe]() {}, 10000, now);
    probe.reset();
    ASSERT_FALSE(weak.expired());

    core.Clear();
    EXPECT_TRUE(weak.expired()) << "Clear() must drop every pending closure";
    EXPECT_TRUE(core.Empty());
    EXPECT_EQ(-1, core.MinTime(now));
}

TEST(TimerCoreTest, CancelAfterClearIsSafeNoOp) {
    TimerCore core;
    uint64_t now = 1000000;
    uint32_t gen = 0;
    uint32_t idx = core.Arm([]() {}, {}, false, 10000, 0, now, gen);

    core.Clear();
    EXPECT_FALSE(core.IsActive(idx, gen));
    EXPECT_FALSE(core.CancelLocal(idx, gen)) << "handles outliving Clear() must degrade to a no-op";
}

// ---------------------------------------------------------------------------
// Task 11: placement must be relative to the wheel, not to wall clock
//
// The wheel only advances inside Run(), so between two Run() calls it sits
// behind real time by the length of the poll that just elapsed. Arm/Rearm used
// to hand wall-clock `now` to ComputeDest as the placement reference, which
// understates a deadline's distance from the wheel by that lag D. A deadline
// really D + delay away was filed as if it were `delay` away, so:
//   * D + delay >= 256 landed in L0 although L0 only spans 256 ms -> the timer
//     fired up to a full L0 revolution early;
//   * D + delay == 256 landed in L0 slot `current_ms_ & 255`, the slot FireSlot
//     was draining, so a self-rescheduling timer refilled the slot as fast as
//     it was emptied and the drain never terminated.
// ---------------------------------------------------------------------------

TEST(TimerCoreTest, ArmWhileWheelLagsRealTimeDoesNotFireEarly) {
    TimerCore core;
    const uint64_t base = 1000000;
    core.Run(base);  // the wheel adopts `base` as its position

    // Nothing is armed, so the loop blocks; the wheel stays parked at `base`
    // while the clock moves on. Lag D = 200, delay = 100 -> the deadline is
    // 300 ms from the wheel, which is past L0's 256 ms span.
    const uint64_t now = base + 200;
    int fired = 0;
    uint32_t gen = 0;
    core.Arm([&fired]() { ++fired; }, {}, false, 100, 0, now, gen);

    core.Run(now + 99);
    EXPECT_EQ(0, fired) << "a 100 ms timer must not fire 256 ms early just because the wheel lagged";

    core.Run(now + 100);
    EXPECT_EQ(1, fired) << "and it must still fire on its own deadline";
}

TEST(TimerCoreTest, DetachedTimerArmedWhileWheelLagsDoesNotFireEarly) {
    TimerCore core;
    const uint64_t base = 1000000;
    core.Run(base);

    const uint64_t now = base + 200;
    int fired = 0;
    core.ArmDetached([&fired]() { ++fired; }, 100, now);

    core.Run(now + 99);
    EXPECT_EQ(0, fired) << "ArmDetached must use the same placement reference as Arm";
    core.Run(now + 100);
    EXPECT_EQ(1, fired);
}

TEST(TimerCoreTest, RearmWhileWheelLagsRealTimeDoesNotFireEarly) {
    TimerCore core;
    const uint64_t base = 1000000;
    uint32_t gen = 0;
    int fired = 0;
    uint32_t idx = core.Arm([&fired]() { ++fired; }, {}, false, 10000, 0, base, gen);

    const uint64_t now = base + 200;
    ASSERT_TRUE(core.Rearm(idx, gen, 100, now));

    core.Run(now + 99);
    EXPECT_EQ(0, fired);
    core.Run(now + 100);
    EXPECT_EQ(1, fired);
}

TEST(TimerCoreTest, TimerArmedFromInsideACallbackIsNotReplayedInTheSamePass) {
    TimerCore core;
    const uint64_t base = 1000000;
    core.Run(base);

    // While the callback runs the wheel sits on base+10 (the slot being fired)
    // and the clock reads base+166, i.e. D = 156. 156 + 100 == 256, which is
    // exactly the alias that used to fold the new node back into slot
    // `(base + 10) & 255`.
    const uint64_t now = base + 166;
    int outer = 0;
    int inner = 0;
    uint32_t g_outer = 0;

    core.Arm(
        [&]() {
            ++outer;
            uint32_t g = 0;
            core.Arm([&inner]() { ++inner; }, {}, false, 100, 0, now, g);
        },
        {}, false, 10, 0, base, g_outer);

    core.Run(now);
    EXPECT_EQ(1, outer);
    EXPECT_EQ(0, inner) << "the new timer is 100 ms out; it must not be replayed by the pass that armed it";

    core.Run(now + 99);
    EXPECT_EQ(0, inner);
    core.Run(now + 100);
    EXPECT_EQ(1, inner) << "and it must still fire on its own deadline";
}

TEST(TimerCoreTest, SelfReschedulingCallbackDoesNotSpinInsideOneSlot) {
    TimerCore core;
    const uint64_t base = 1000000;
    core.Run(base);

    // The http3 stream-cleanup timer shape: a one-shot whose callback re-arms
    // itself for the same period. This spun ~800 times per slot in production.
    const uint64_t now = base + 166;
    int fired = 0;
    std::function<void()> reschedule;
    reschedule = [&]() {
        ++fired;
        if (fired > 1000) {
            return;  // safety valve: fail the assertion instead of hanging the suite
        }
        uint32_t g = 0;
        core.Arm(reschedule, {}, false, 100, 0, now, g);
    };

    uint32_t gen = 0;
    core.Arm(reschedule, {}, false, 10, 0, base, gen);

    core.Run(now);
    EXPECT_EQ(1, fired) << "a 100 ms self-rescheduling timer must fire once per pass, not drain-and-refill the slot";
}

TEST(TimerCoreTest, PlacementIsCorrectForEveryWheelLag) {
    // Sweep the wheel/clock lag across a full L0 revolution and beyond. Every
    // timer must fire on its own deadline and never before it, whatever the lag.
    for (uint64_t lag = 0; lag <= 300; ++lag) {
        for (uint32_t delay : {1u, 50u, 100u, 255u, 256u, 257u, 1000u}) {
            TimerCore core;
            const uint64_t base = 1000000;
            core.Run(base);

            const uint64_t now = base + lag;
            int fired = 0;
            uint32_t gen = 0;
            core.Arm([&fired]() { ++fired; }, {}, false, delay, 0, now, gen);

            const uint64_t deadline = now + delay;
            core.Run(deadline - 1);
            ASSERT_EQ(0, fired) << "fired early with lag=" << lag << " delay=" << delay;
            core.Run(deadline);
            ASSERT_EQ(1, fired) << "missed its deadline with lag=" << lag << " delay=" << delay;
        }
    }
}

}  // namespace
}  // namespace common
}  // namespace quicx
