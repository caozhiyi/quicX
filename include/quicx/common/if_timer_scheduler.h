#ifndef COMMON_IF_TIMER_SCHEDULER
#define COMMON_IF_TIMER_SCHEDULER

#include <cstdint>
#include <functional>
#include <memory>

namespace quicx {
namespace common {

// Internal type; only forward-declared here on purpose. Code outside the
// library cannot obtain a TimerCore*, which is what keeps Timer's internal
// constructor effectively private while still letting the handle stay a plain
// 16-byte value type instead of a heap-allocated interface.
class TimerCore;

/**
 * @brief Move-only RAII handle for a cancellable timer.
 *
 * Contract:
 *   1. Destruction cancels. After ~Timer() returns, the callback will not be
 *      invoked. (A callback already executing cannot be interrupted -- the same
 *      guarantee level as Linux del_timer as opposed to del_timer_sync. That
 *      residual case is covered by the owner guard passed to AddTimer.)
 *   2. Any thread may destroy or cancel the handle. On the owning event loop
 *      thread the cancel is synchronous; from another thread it is queued and
 *      applied by the loop before it runs any further callback.
 *   3. The handle is the sole owner of its timer slot, so no stale duplicate
 *      handle can exist and cancelling twice is a safe no-op.
 *
 * Rearm() is deliberately NOT virtual and does not go through
 * ITimerScheduler: it is the per-packet hot path (PTO and idle-timeout reset)
 * and dispatches straight to the owning TimerCore.
 */
class Timer {
public:
    Timer() noexcept = default;

    /**
     * @brief Internal constructor. Use ITimerScheduler::AddTimer instead.
     */
    Timer(TimerCore* core, uint32_t index, uint32_t gen) noexcept;

    ~Timer();

    Timer(Timer&& other) noexcept;
    Timer& operator=(Timer&& other) noexcept;

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    /// True while the timer is armed and has not fired yet.
    bool IsActive() const noexcept;

    /// Cancel now. Idempotent; safe from any thread.
    void Cancel() noexcept;

    /**
     * @brief Reschedule in place: the timer node is spliced between wheel slots
     *        rather than freed and reallocated, and the owner guard is not
     *        re-copied.
     *
     * @param delay_ms new delay
     * @param now      current time in ms; 0 means "read the clock"
     * @return false if the handle is empty or was already cancelled
     */
    bool Rearm(uint32_t delay_ms, uint64_t now = 0) noexcept;

private:
    TimerCore* core_ = nullptr;
    uint32_t index_ = 0;
    uint32_t gen_ = 0;
};

/**
 * @brief Narrow scheduling interface: everything a component needs in order to
 *        run something later, and nothing else.
 *
 * Components such as RecvControl (delayed ACK), SendControl (PTO) and
 * SendManager (pacing, flow-control recheck) depend on this instead of the full
 * IEventLoop. Rationale:
 *   * they have no business seeing RegisterFd / PostTask / RunInLoop;
 *   * a three-method interface is cheap to fake in unit tests;
 *   * the thread-affinity assertion and the owner guard still have exactly one
 *     implementation, which is what removed the two-entry-point split that
 *     produced the idle-timer use-after-free.
 *
 * Threading: all three methods must be called on the owning loop thread. The
 * returned handle, by contrast, may be destroyed from any thread.
 */
class ITimerScheduler {
public:
    virtual ~ITimerScheduler() = default;

    /**
     * @brief Schedule a one-shot callback and return a handle that cancels it.
     *
     * @param owner    lifetime guard. The callback is skipped if `owner` has
     *                 expired by the time the timer fires, which is what makes
     *                 it safe for callbacks to reference their owning object.
     * @param cb       callback, invoked on the loop thread
     * @param delay_ms delay in milliseconds
     */
    [[nodiscard]] virtual Timer AddTimer(std::weak_ptr<void> owner, std::function<void()> cb, uint32_t delay_ms) = 0;

    /// Same as AddTimer but re-arms itself after every firing until cancelled.
    [[nodiscard]] virtual Timer AddRepeatTimer(std::weak_ptr<void> owner, std::function<void()> cb,
        uint32_t interval_ms) = 0;

    /**
     * @brief Fire-and-forget: run `cb` once after `delay_ms`, with no way to
     *        cancel it.
     *
     * This exists so that "I do not need to cancel this" is expressible without
     * a handle. Returning a handle for such call sites would invite
     * `loop->AddTimer(...);` with the result discarded, which would destroy the
     * handle immediately and cancel the timer on the spot.
     */
    virtual void PostDelayed(std::function<void()> cb, uint32_t delay_ms) = 0;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_IF_TIMER_SCHEDULER
