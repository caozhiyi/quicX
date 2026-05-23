// Tests for SendManager's connection-level flow-control recheck timer
// (Bug #17 fix). Verifies that SetFlowControlBlocked() schedules a
// ~100 ms wake-up that fires send_retry_cb_, that re-arming while a
// timer is pending does NOT double-schedule, that one-shot semantics
// hold, and that ClearActiveStreams() disarms the timer.
//
// Implementation note on time sources:
//   SendManager::SetFlowControlBlocked() calls
//   `timer_->AddTimer(flow_control_recheck_task_, 100)` *without* an
//   explicit reference time. TimingWheelTimer::AddTimer() then computes
//   the absolute deadline from the live wall clock (UTCTimeMsec()).
//   For that reason the test cannot fabricate a virtual clock — driving
//   the wheel with arbitrary reference timestamps would never match the
//   absolute deadlines stored in the wheel slots.
//
//   We instead measure real wall-clock time and add a small slack to the
//   thresholds so we don't get flaky failures on busy CI. The recheck
//   interval (100 ms) is large enough that 20–30 ms slack is invisible
//   in practice.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "common/timer/timer_task.h"
#include "common/timer/timing_wheel_timer.h"
#include "common/util/time.h"
#include "quic/connection/controler/send_manager.h"

namespace quicx {
namespace quic {
namespace {

constexpr uint32_t kRecheckIntervalMs = 100;  // mirrors send_manager.cpp

// Slack for the "should NOT have fired yet" check. Must be small enough
// that 100ms - kSlackMs is still meaningfully before 100ms.
constexpr uint32_t kSlackMs = 25;

class SendManagerRecheckTimerTest: public ::testing::Test {
protected:
    void SetUp() override {
        timer_ = std::make_shared<common::TimingWheelTimer>();
        send_manager_ = std::make_unique<SendManager>(timer_);
        retry_count_ = 0;
        send_manager_->SetSendRetryCallBack([this]() { retry_count_.fetch_add(1); });
    }

    // Sleep for the given duration in real time, then drive the timing wheel
    // forward to the current wall clock so any deadlines that have elapsed
    // fire their callbacks. Returns the wall-clock millisecond after the
    // wheel has been ticked.
    uint64_t SleepAndTick(uint32_t ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        timer_->TimerRun();
        return common::UTCTimeMsec();
    }

    std::shared_ptr<common::TimingWheelTimer> timer_;
    std::unique_ptr<SendManager> send_manager_;
    std::atomic<int> retry_count_{0};
};

// ---------------------------------------------------------------------------
// 1. Baseline timing: timer must NOT fire well before 100 ms,
//    MUST fire by/near 100 ms.
// ---------------------------------------------------------------------------
TEST_F(SendManagerRecheckTimerTest, FiresExactlyAfter100Ms) {
    send_manager_->SetFlowControlBlocked();

    // ~50ms after schedule – well below the 100ms boundary.
    SleepAndTick(50);
    EXPECT_EQ(retry_count_.load(), 0)
        << "recheck timer fired too early (before ~75 ms)";

    // ~75 ms total – still below the boundary minus slack.
    SleepAndTick(kRecheckIntervalMs - 50 - kSlackMs);
    EXPECT_EQ(retry_count_.load(), 0)
        << "recheck timer fired before 100ms - " << kSlackMs << " ms";

    // Push past 100 ms total. Use generous extra (50 ms) to absorb scheduler
    // latency on busy CI hosts. The timer MUST have fired exactly once.
    SleepAndTick(kSlackMs + 50);
    EXPECT_EQ(retry_count_.load(), 1)
        << "recheck timer did not fire after the 100 ms interval elapsed";
}

// ---------------------------------------------------------------------------
// 2. The timer is one-shot: after firing, no further callbacks until the
//    flag is set again via another SetFlowControlBlocked() call.
// ---------------------------------------------------------------------------
TEST_F(SendManagerRecheckTimerTest, OneShotDoesNotKeepFiring) {
    send_manager_->SetFlowControlBlocked();

    // Wait long enough for the first fire (with margin).
    SleepAndTick(kRecheckIntervalMs + 50);
    EXPECT_EQ(retry_count_.load(), 1);

    // Run the wheel for another ~3 intervals. No rescheduling, no extra fires.
    SleepAndTick(kRecheckIntervalMs * 3);
    EXPECT_EQ(retry_count_.load(), 1)
        << "recheck timer fired more than once for a single SetFlowControlBlocked";
}

// ---------------------------------------------------------------------------
// 3. Re-entry while a timer is already pending must not double-schedule.
//    Two calls within the same 100 ms window must still produce exactly
//    one callback.
// ---------------------------------------------------------------------------
TEST_F(SendManagerRecheckTimerTest, ReentrantSetDoesNotDoubleSchedule) {
    send_manager_->SetFlowControlBlocked();

    // Half-way through the interval, call again — must be a no-op for the
    // timer wheel (the existing pending task stays).
    SleepAndTick(50);
    EXPECT_EQ(retry_count_.load(), 0);
    send_manager_->SetFlowControlBlocked();

    // Wait until well past the original 100 ms deadline.
    SleepAndTick(kRecheckIntervalMs);
    EXPECT_EQ(retry_count_.load(), 1) << "expected exactly one fire";

    // And no second fire after that.
    SleepAndTick(kRecheckIntervalMs * 2);
    EXPECT_EQ(retry_count_.load(), 1)
        << "double-schedule produced an extra fire";
}

// ---------------------------------------------------------------------------
// 4. After the first fire, the system can re-enter the blocked state again
//    (e.g., TrySend re-runs and FC still blocks). A fresh
//    SetFlowControlBlocked must produce a brand-new ~100 ms wake-up.
// ---------------------------------------------------------------------------
TEST_F(SendManagerRecheckTimerTest, RescheduleAfterFireWorks) {
    send_manager_->SetFlowControlBlocked();
    SleepAndTick(kRecheckIntervalMs + 50);
    EXPECT_EQ(retry_count_.load(), 1);

    // Re-arm. A second fire must occur ~100 ms later.
    send_manager_->SetFlowControlBlocked();

    // Just before 100 ms post-rearm, must not have fired again.
    SleepAndTick(kRecheckIntervalMs - kSlackMs);
    EXPECT_EQ(retry_count_.load(), 1)
        << "second fire happened too early after re-arm";

    // After the boundary plus margin, must have fired the second time.
    SleepAndTick(kSlackMs + 50);
    EXPECT_EQ(retry_count_.load(), 2)
        << "re-armed recheck timer never fired";
}

// ---------------------------------------------------------------------------
// 5. ClearActiveStreams() must disarm the recheck timer so the callback
//    does not fire on a connection that is being torn down.
// ---------------------------------------------------------------------------
TEST_F(SendManagerRecheckTimerTest, ClearActiveStreamsDisarmsRecheckTimer) {
    send_manager_->SetFlowControlBlocked();
    SleepAndTick(50);
    EXPECT_EQ(retry_count_.load(), 0);

    // Connection close path. With null stream_manager_ this exercises the
    // wait_frame_list_ + ClearRetransmissionData() + recheck-timer-disarm
    // path that Bug #17 specifically defends.
    send_manager_->ClearActiveStreams();

    // Run well past the original 100 ms deadline. The recheck callback
    // must NOT fire.
    SleepAndTick(kRecheckIntervalMs * 3);
    EXPECT_EQ(retry_count_.load(), 0)
        << "recheck timer fired after ClearActiveStreams() — disarm failed";
}

// ---------------------------------------------------------------------------
// 6. Re-arming after a fire should not leave the bookkeeping inconsistent:
//    the second arm must fire at its own ~100 ms deadline, not immediately
//    nor after a stale offset.
// ---------------------------------------------------------------------------
TEST_F(SendManagerRecheckTimerTest, RearmAfterFireUsesFreshInterval) {
    send_manager_->SetFlowControlBlocked();
    SleepAndTick(kRecheckIntervalMs + 50);
    ASSERT_EQ(retry_count_.load(), 1);

    // Wait an extra long pause then re-arm. The next fire must be ~100 ms
    // from the re-arm point — a stale offset would fire much sooner.
    SleepAndTick(200);
    EXPECT_EQ(retry_count_.load(), 1) << "stale timer fired during quiet period";

    send_manager_->SetFlowControlBlocked();

    // Below 100 ms post-rearm — must not fire yet.
    SleepAndTick(kRecheckIntervalMs - kSlackMs);
    EXPECT_EQ(retry_count_.load(), 1)
        << "re-armed timer fired immediately — stale bookkeeping suspected";

    // Past 100 ms post-rearm — fires on schedule.
    SleepAndTick(kSlackMs + 50);
    EXPECT_EQ(retry_count_.load(), 2);
}

}  // namespace
}  // namespace quic
}  // namespace quicx
