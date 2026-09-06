#ifndef COMMON_NETWORK_EVENT_LOOP
#define COMMON_NETWORK_EVENT_LOOP

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/network/if_event_driver.h"
#include "common/network/if_event_loop.h"
#include "common/timer/timer_core.h"

namespace quicx {
namespace common {

class EventLoop: public IEventLoop, public std::enable_shared_from_this<EventLoop> {
public:
    EventLoop() = default;
    ~EventLoop() = default;

    virtual bool Init() override;
    // Run one iteration: run timers, wait for I/O up to next timer, dispatch callbacks
    // Returns number of I/O events dispatched
    virtual int Wait() override;

    virtual bool RegisterFd(uint32_t fd, int32_t events, std::shared_ptr<IFdHandler> handler) override;
    virtual bool ModifyFd(uint32_t fd, int32_t events) override;
    virtual bool RemoveFd(uint32_t fd) override;

    virtual void AddFixedProcess(std::function<void()> cb) override;
    virtual void AddFixedProcess(std::weak_ptr<void> owner, std::function<void()> cb) override;
    virtual void ClearFixedProcesses() override;

    // ---- ITimerScheduler (handle-based API) ----
    Timer AddTimer(std::weak_ptr<void> owner, std::function<void()> cb, uint32_t delay_ms) override;
    Timer AddRepeatTimer(std::weak_ptr<void> owner, std::function<void()> cb, uint32_t interval_ms) override;
    void PostDelayed(std::function<void()> cb, uint32_t delay_ms) override;

    virtual void ClearAllTimers() override;

    virtual void PostTask(std::function<void()> fn) override;
    virtual void Wakeup() override;

    // Check if current thread is the loop thread
    virtual bool IsInLoopThread() const override;

    // Execute task immediately if in loop thread, otherwise post it
    virtual void RunInLoop(std::function<void()> task) override;

    // Assert that current thread is loop thread
    virtual void AssertInLoopThread() override;

private:
    void DrainPostedTasks();

    std::unique_ptr<IEventDriver> driver_;

    // The one real timer engine. Held by shared_ptr (not by value) so that Timer
    // handles -- which keep only a weak_ptr to it -- can outlive this EventLoop
    // during teardown. A connection's Timer handle may be destroyed on a different
    // thread after the loop thread has exited; with a value member that would be a
    // use-after-free when the handle dereferences the freed core. The shared_ptr
    // keeps the core alive until the last handle is gone, and weak_ptr handles
    // simply no-op once it is.
    std::shared_ptr<TimerCore> timer_core_;

    std::vector<Event> events_;

    // NOTE: timers_ / timer_ids_ / timer_repeat_ are gone. They existed because
    // the wheel could not tell the loop when a one-shot fired, so ClearAllTimers
    // had to walk every id ever handed out -- an unordered_set that grew with the
    // *cumulative* number of timers (QUIC arms one PTO per packet). TimerCore
    // owns its own nodes and offers Clear(), so the bookkeeping is unnecessary.
    std::unordered_map<uint32_t, std::weak_ptr<IFdHandler>> fd_to_handler_;

    std::mutex tasks_mu_;
    std::deque<std::function<void()>> tasks_;
    // Reusable scratch deque used by DrainPostedTasks() to swap-out the
    // pending tasks under lock. Keeping it as a member avoids constructing
    // and destructing a fresh std::deque (and the std::function moves it
    // contains) on every event loop iteration.
    std::deque<std::function<void()>> tasks_drain_scratch_;

    // Legacy un-guarded callbacks (deprecated path, kept during migration)
    std::vector<std::function<void()>> fixed_processes_;
    // Lifetime-guarded callbacks: only fire while owner is alive
    std::vector<std::pair<std::weak_ptr<void>, std::function<void()>>> guarded_fixed_processes_;

    bool initialized_ = false;
    std::thread::id thread_id_;

    // Optimization: avoid pipe write for same-thread wakeup
    // When AddTimer/PostTask called from event loop thread, just set this flag
    // to make next Wait() use timeout=0 instead of writing to wakeup pipe
    bool need_immediate_wakeup_ = false;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_NETWORK_EVENT_LOOP
