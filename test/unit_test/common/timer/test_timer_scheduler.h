#ifndef TEST_UNIT_TEST_COMMON_TIMER_TEST_TIMER_SCHEDULER
#define TEST_UNIT_TEST_COMMON_TIMER_TEST_TIMER_SCHEDULER

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

#include "common/timer/if_timer_scheduler.h"
#include "common/timer/timer_core.h"
#include "common/util/time.h"

namespace quicx {
namespace common {

/**
 * @brief Test double for ITimerScheduler, backed by a real TimerCore.
 *
 * It replaces the hand-rolled MockTimer classes that used to be copy-pasted
 * across the connection and stream tests. Those recorded TimerTask copies in a
 * vector and never fired anything, so a test could only observe "AddTimer was
 * called". This one runs the production engine, which means:
 *   * timers actually fire, at the right time, when the test advances the clock;
 *   * cancellation is observable as a drop in PendingCount(), including the
 *     cancellations a component performs through its own Timer handle (those are
 *     invisible to a scheduler-level mock by construction);
 *   * the owner guard is honoured, so a test can verify that a callback is
 *     skipped once its owner is gone.
 */
class TestTimerScheduler: public ITimerScheduler {
public:
    TestTimerScheduler():
        now_(MonotonicTimeMsec()) {}

    Timer AddTimer(std::weak_ptr<void> owner, std::function<void()> cb, uint32_t delay_ms) override {
        ++arm_count_;
        uint32_t gen = 0;
        uint32_t index =
            core_->Arm(std::move(cb), std::move(owner), /*has_owner=*/true, delay_ms, /*interval_ms=*/0, Now(), gen);
        return Timer(core_, index, gen);
    }

    Timer AddRepeatTimer(std::weak_ptr<void> owner, std::function<void()> cb, uint32_t interval_ms) override {
        ++arm_count_;
        uint32_t gen = 0;
        uint32_t index =
            core_->Arm(std::move(cb), std::move(owner), /*has_owner=*/true, interval_ms, interval_ms, Now(), gen);
        return Timer(core_, index, gen);
    }

    void PostDelayed(std::function<void()> cb, uint32_t delay_ms) override {
        ++arm_count_;
        core_->ArmDetached(std::move(cb), delay_ms, Now());
    }

    // ---- test controls ----

    /// Move the clock forward and fire everything that becomes due.
    ///
    /// The FIRST Advance() freezes the clock: from then on Now() is purely
    /// virtual (freeze-point + every Advance since) and never follows real
    /// time again. While the clock follows real time (before the first
    /// Advance), a slow build (TSan) can cross a millisecond boundary between
    /// two Advance() calls, silently shifting every later virtual deadline by
    /// that amount — e.g. CryptoSlowPathTest expected NextDeadlineMs() ==
    /// t0 + 160 and got t0 + 161. Suites that assert exact virtual deadlines
    /// should freeze the clock in SetUp() via Advance(0), before anything
    /// arms or samples the clock.
    void Advance(uint64_t ms) {
        if (!frozen_) {
            // Sync once with real time at the freeze point, so timers armed
            // BEFORE this moment (deadline = real + delay) still fire on the
            // virtual clock.
            now_ = Now();
            frozen_ = true;
        }
        now_ += ms;
        core_->Run(now_);
    }

    /// Fire anything already due without moving the clock.
    void RunDue() { core_->Run(Now()); }

    /// Number of Add*/PostDelayed calls seen so far.
    uint32_t ArmCount() const { return arm_count_; }
    /// Timers currently armed (fired or cancelled ones are not counted).
    uint32_t PendingCount() const { return core_->PendingCount(); }
    bool Empty() const { return core_->Empty(); }
    int32_t MinTime() { return core_->MinTime(Now()); }

    uint64_t Now() const {
        if (frozen_) {
            return now_;
        }
        // Follow real time until the first Advance(): tests that never
        // Advance (real-sleep drivers, e.g. SendManagerRecheckTimerTest) keep
        // seeing the production clock and RunDue() fires whatever real time
        // has made due. Once frozen, Now() is exactly Advance-controlled.
        //
        // Monotonic, matching the production EventLoop clock domain (see
        // EventLoop::Wait/AddTimer). Mixing wall clock here would compare
        // deadlines from different epochs inside TimerCore.
        uint64_t real = MonotonicTimeMsec();
        return real > now_ ? real : now_;
    }

    TimerCore& Core() { return *core_; }

private:
    std::shared_ptr<TimerCore> core_ = std::make_shared<TimerCore>();
    uint64_t now_;
    bool frozen_ = false;
    uint32_t arm_count_ = 0;
};

}  // namespace common
}  // namespace quicx

#endif  // TEST_UNIT_TEST_COMMON_TIMER_TEST_TIMER_SCHEDULER
