#include <cstdlib>
#include <functional>
#include <thread>
#include <vector>

#include "common/log/log.h"
#include "common/network/event_loop.h"
#include "common/util/time.h"

#include "quic/quicx/global_resource.h"

namespace quicx {
namespace common {

bool EventLoop::Init() {
    if (initialized_) {
        return true;
    }

    driver_ = IEventDriver::Create();
    if (!driver_) {
        LOG_ERROR("Failed to create event driver");
        return false;
    }
    if (!driver_->Init()) {
        LOG_ERROR("Failed to init event driver");
        return false;
    }
    events_.reserve(driver_->GetMaxEvents());
    initialized_ = true;
    thread_id_ = std::this_thread::get_id();
    // Declare the serialisation domain of the (deliberately lock-free) timer
    // engine. This is what lets Timer::Cancel() decide between unlinking the
    // node synchronously and queueing the cancel for this thread to apply.
    timer_core_.BindLoopThread(thread_id_);

    // Register this event loop for the current thread (for lock-free pool operations)
    // In test environments EventLoop may be stack-allocated, so we need to handle that case
    try {
        quic::GlobalResource::Instance().RegisterThreadEventLoop(shared_from_this());
    } catch (const std::bad_weak_ptr&) {
        // EventLoop is not managed by shared_ptr (e.g., in unit tests) - skip registration
        // This is fine for testing since those tests don't use GlobalResource
    }

    return true;
}

bool EventLoop::IsInLoopThread() const {
    return std::this_thread::get_id() == thread_id_;
}

void EventLoop::RunInLoop(std::function<void()> task) {
    if (IsInLoopThread()) {
        task();
    } else {
        PostTask(std::move(task));
    }
}

void EventLoop::AssertInLoopThread() {
    if (!IsInLoopThread()) {
        // CRITICAL: previously this only emitted LOG_FATAL (which is non-fatal
        // in our logger — see common/log/base_logger.cpp). That left thread
        // safety bugs silent and caused cascading CONNECTION_CLOSE storms in
        // long perf runs (root cause for the 5s/10s stalls observed on
        // 2026-05-29). We now log AND abort so any cross-thread access is
        // surfaced immediately with a stack trace from the crashed thread.
        LOG_FATAL("EventLoop accessed from wrong thread! expected_tid_hash=%zu actual_tid_hash=%zu",
            std::hash<std::thread::id>{}(thread_id_), std::hash<std::thread::id>{}(std::this_thread::get_id()));
        // Flush pending log records so the FATAL line is on disk before abort.
        std::abort();
    }
}

int EventLoop::Wait() {
    uint64_t now = UTCTimeMsec();
    timer_core_.Run(now);

    int32_t next_ms = timer_core_.MinTime(now);
    int timeout_ms = next_ms >= 0 ? static_cast<int>(next_ms) : 1000;

    // Check if same-thread wakeup requested (e.g., from AddTimer/PostTask)
    // If so, use timeout=0 to return immediately instead of blocking
    if (need_immediate_wakeup_) {
        timeout_ms = 0;
        need_immediate_wakeup_ = false;  // Clear flag
    }

    // Cross-thread wakeup safety: if tasks were posted while the driver was
    // not yet initialized (race between PostTask on the creator thread and
    // the loop thread reaching Init()/first Wait()), the Wakeup() eventfd
    // write may have been a no-op. Without this guard, the first Wait() on
    // the loop thread would block for the full default timeout (1000ms)
    // even though tasks are already queued — this was a major latency
    // source for short-lived clients (e.g. each Handshake_NewConnection
    // iteration paid ~1s during client Init()). Checking the queue here
    // turns that case into an immediate drain at negligible cost.
    if (timeout_ms > 0) {
        std::lock_guard<std::mutex> lk(tasks_mu_);
        if (!tasks_.empty()) {
            timeout_ms = 0;
        }
    }

    // Diagnostic: detect driver_->Wait() over-running its requested timeout
    // by more than a 100 ms slack. This was the smoking gun for Bug #21
    // (TimingWheelTimer cache corruption hiding short timers behind 10 s
    // idle timers). Cheap (one extra UTCTimeMsec call + one branch) and
    // invaluable if the symptom ever recurs from another root cause.
    uint64_t enter_wait_ms = UTCTimeMsec();

    int n = driver_->Wait(events_, timeout_ms);

    if (timeout_ms >= 0) {
        int64_t blocked_ms = static_cast<int64_t>(UTCTimeMsec()) - static_cast<int64_t>(enter_wait_ms);
        if (blocked_ms > timeout_ms + 100) {
            LOG_ERROR(
                "EventLoop::Wait: driver overran timeout (blocked=%lldms, "
                "requested timeout=%d ms, next_timer=%d ms, n=%d) — "
                "possible timer-cache regression",
                (long long)blocked_ms, timeout_ms, next_ms, n);
        }
    }
    if (n < 0) {
        LOG_ERROR("Event driver wait failed");
        return -1;
    }

    // Run timers AGAIN after the driver returned. The first TimerRun() above
    // happened before we blocked, so any task whose deadline elapsed during
    // driver_->Wait(timeout_ms) has not been fired yet. Without this second
    // pass, a single Wait() iteration that blocks until a timer's deadline
    // would return WITHOUT firing the timer — the callback would only run
    // on the *next* Wait() entry. For pure-timer self-driving (no fd
    // activity, no PostTask) that means the timer is effectively delayed
    // by one extra Wait() round-trip, and in environments that call Wait()
    // only when prompted by I/O (e.g. unit tests, idle servers) it can be
    // delayed indefinitely. This was part of the same family of bugs as
    // the AddTimer-Wakeup() issue fixed above.
    timer_core_.Run(UTCTimeMsec());

    // handle events
    for (int i = 0; i < n; i++) {
        auto& ev = events_[i];
        auto it = fd_to_handler_.find(ev.fd);
        if (it == fd_to_handler_.end()) {
            LOG_ERROR("No handler found for fd %d", ev.fd);
            continue;
        }
        auto handler = it->second.lock();
        if (!handler) {
            LOG_ERROR("Handler expired for fd %d", ev.fd);
            fd_to_handler_.erase(it);
            continue;
        }
        switch (ev.type) {
            case EventType::ET_READ:
                handler->OnRead(ev.fd);
                break;
            case EventType::ET_WRITE:
                handler->OnWrite(ev.fd);
                break;
            case EventType::ET_ERROR:
                handler->OnError(ev.fd);
                break;
            case EventType::ET_CLOSE:
                handler->OnClose(ev.fd);
                break;
        }
    }

    // handle fixed processes (legacy un-guarded)
    for (auto& cb : fixed_processes_) {
        cb();
    }

    // handle guarded fixed processes: skip & remove expired owners
    {
        auto it = guarded_fixed_processes_.begin();
        while (it != guarded_fixed_processes_.end()) {
            if (it->first.lock()) {
                it->second();
                ++it;
            } else {
                // Owner expired — remove this entry
                it = guarded_fixed_processes_.erase(it);
            }
        }
    }

    DrainPostedTasks();
    return n;
}

bool EventLoop::RegisterFd(uint32_t fd, int32_t events, std::shared_ptr<IFdHandler> handler) {
    AssertInLoopThread();
    // Validate before storing to avoid residual entries on failure
    if (!handler) {
        LOG_ERROR("Handler is null for fd %d", fd);
        return false;
    }
    if (!driver_) {
        LOG_ERROR("Event loop driver is not initialized for fd %d", fd);
        return false;
    }
    if (!driver_->AddFd(fd, events)) {
        LOG_ERROR("Failed to add fd %d to event driver", fd);
        return false;
    }
    fd_to_handler_[fd] = handler;
    return true;
}

bool EventLoop::ModifyFd(uint32_t fd, int32_t events) {
    AssertInLoopThread();
    if (!driver_) {
        LOG_ERROR("Event loop driver is not initialized for fd %d", fd);
        return false;
    }
    return driver_->ModifyFd(fd, events);
}

bool EventLoop::RemoveFd(uint32_t fd) {
    AssertInLoopThread();
    fd_to_handler_.erase(fd);
    if (!driver_) {
        LOG_ERROR("Event loop driver is not initialized for fd %d", fd);
        return false;
    }
    return driver_->RemoveFd(fd);
}

void EventLoop::AddFixedProcess(std::function<void()> cb) {
    AssertInLoopThread();
    fixed_processes_.push_back(cb);
}

void EventLoop::AddFixedProcess(std::weak_ptr<void> owner, std::function<void()> cb) {
    AssertInLoopThread();
    guarded_fixed_processes_.emplace_back(std::move(owner), std::move(cb));
}

void EventLoop::ClearFixedProcesses() {
    // NOTE: this releases any shared_ptr captured by fixed-process
    // std::bind/lambda closures (typically a worker shared_ptr). The caller
    // is expected to run this after the event loop has stopped iterating, so
    // no in-flight Process() invocation is still referring to the worker.
    fixed_processes_.clear();
    guarded_fixed_processes_.clear();
}

void EventLoop::ClearAllTimers() {
    // Releases every pending timer callback. That is the only way to drop the
    // shared_ptr<BaseConnection> captured by the Closing/Draining
    //   AddTimer([self = shared_from_this()]{ self->OnClosingTimeout(); }, ...)
    // call sites once the loop has stopped iterating: those callbacks never fire
    // again, so without this their strong self-references pin BaseConnection
    // (and, through it, the EventLoop) forever -- the P4 per-connection residue.
    //
    // MUST be called after Stop()/Join(): TimerCore is single-threaded. Safe
    // teardown order: stop loop -> join loop thread -> ClearFixedProcesses() ->
    // ClearAllTimers() -> drop the owner's shared_ptr<EventLoop>.
    //
    // This used to walk an unordered_set of every id ever returned by AddTimer,
    // synthesising a probe TimerTask per id -- O(cumulative timers). TimerCore
    // owns its nodes, so it can drop them all directly.
    timer_core_.Clear();

    // Also drain any posted tasks that were RunInLoop()'d from a different
    // thread. Each posted task may capture shared_ptr<Stream>/<Connection>
    // via [self = shared_from_this()]; if the loop stops before the task
    // is drained, those captures survive and pin the object forever.
    {
        std::lock_guard<std::mutex> lk(tasks_mu_);
        tasks_.clear();
    }
}

Timer EventLoop::AddTimer(std::weak_ptr<void> owner, std::function<void()> cb, uint32_t delay_ms) {
    AssertInLoopThread();
    uint32_t gen = 0;
    uint32_t index = timer_core_.Arm(
        std::move(cb), std::move(owner), /*has_owner=*/true, delay_ms, /*interval_ms=*/0, UTCTimeMsec(), gen);
    if (index == TimerCore::kNoEntry) {
        LOG_ERROR("EventLoop::AddTimer: timer slab exhausted");
        return Timer();
    }
    // NOTE: do NOT call Wakeup() here.
    //
    // Arming is gated by AssertInLoopThread(), so we are always on the loop
    // thread, which means Wait() is NOT currently blocking on the driver (we are
    // between iterations, inside an event/timer/task callback). The very next
    // Wait() reads MinTime(now) and passes the freshly-armed deadline straight
    // into driver_->Wait(timeout). No wakeup is needed, and triggering one is
    // actively harmful: the same-thread branch of Wakeup() sets
    // need_immediate_wakeup_, which forces the next Wait() to use timeout=0 and
    // defeats deadline-driven blocking. In the field that manifested as a ~9 s
    // silence in the PTO path -- every outgoing packet re-armed pto_timer_, and
    // the resulting flag turned every Wait() into a 0-timeout poll, so the wheel
    // was never advanced enough to fire the 88 ms PTO when no I/O was pending.
    return Timer(&timer_core_, index, gen);
}

Timer EventLoop::AddRepeatTimer(std::weak_ptr<void> owner, std::function<void()> cb, uint32_t interval_ms) {
    AssertInLoopThread();
    if (interval_ms == 0) {
        LOG_ERROR("EventLoop::AddRepeatTimer: interval must be non-zero");
        return Timer();
    }
    uint32_t gen = 0;
    uint32_t index =
        timer_core_.Arm(std::move(cb), std::move(owner), /*has_owner=*/true, interval_ms, interval_ms, UTCTimeMsec(), gen);
    if (index == TimerCore::kNoEntry) {
        LOG_ERROR("EventLoop::AddRepeatTimer: timer slab exhausted");
        return Timer();
    }
    return Timer(&timer_core_, index, gen);
}

void EventLoop::PostDelayed(std::function<void()> cb, uint32_t delay_ms) {
    AssertInLoopThread();
    // No handle, hence no slab entry and nothing for the caller to keep alive.
    timer_core_.ArmDetached(std::move(cb), delay_ms, UTCTimeMsec());
}

void EventLoop::PostTask(std::function<void()> fn) {
    bool need_wakeup = false;
    {
        std::lock_guard<std::mutex> lk(tasks_mu_);
        // Only need to wakeup if the queue was empty before adding this task
        // If queue already had tasks, the event loop  will drain them in current or next iteration
        need_wakeup = tasks_.empty();
        tasks_.push_back(std::move(fn));
    }
    // Only wakeup if:
    // 1. Queue was empty (so event loop might be waiting)
    // 2. We're not already in the loop thread (would deadlock on wakeup)
    if (need_wakeup && !IsInLoopThread()) {
        Wakeup();
    }
}

void EventLoop::Wakeup() {
    if (IsInLoopThread()) {
        // Same thread: just set flag to make next Wait() return immediately
        // This avoids writing to wakeup pipe 26K+ times during packet loss
        need_immediate_wakeup_ = true;
    } else {
        // Cross-thread: must write pipe to interrupt epoll/kqueue Wait()
        if (driver_) {
            driver_->Wakeup();
        }
    }
}

void EventLoop::DrainPostedTasks() {
    // DrainPostedTasks is called from the loop thread only, so we can safely
    // reuse a single scratch deque across iterations to avoid per-iteration
    // allocation/free of the deque control nodes and the std::function moves.
    {
        std::lock_guard<std::mutex> lk(tasks_mu_);
        if (tasks_.empty()) {
            return;
        }
        tasks_.swap(tasks_drain_scratch_);
    }
    for (auto& fn : tasks_drain_scratch_) {
        if (fn) fn();
    }
    tasks_drain_scratch_.clear();
}

}  // namespace common
}  // namespace quicx
