#include <vector>

#include "common/log/log.h"
#include "common/network/event_loop.h"
#include "common/timer/timer.h"
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
    timer_ = MakeTimer();
    if (!timer_) {
        LOG_ERROR("Failed to create timer");
        return false;
    }
    events_.reserve(driver_->GetMaxEvents());
    initialized_ = true;
    thread_id_ = std::this_thread::get_id();

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
        LOG_FATAL("EventLoop accessed from wrong thread!");
    }
}

int EventLoop::Wait() {
    uint64_t now = UTCTimeMsec();
    timer_->TimerRun(now);

    int32_t next_ms = timer_->MinTime(now);
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
        int64_t blocked_ms = static_cast<int64_t>(UTCTimeMsec()) -
                             static_cast<int64_t>(enter_wait_ms);
        if (blocked_ms > timeout_ms + 100) {
            LOG_ERROR("EventLoop::Wait: driver overran timeout (blocked=%lldms, "
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
    timer_->TimerRun(UTCTimeMsec());

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
    // NOTE: this releases every pending timer callback, which is the only way
    // to drop shared_ptr<BaseConnection> captures made by the Closing/Draining
    // 1.5s AddTimer([self]() { self->OnClosingTimeout(); }, ...) call sites
    // after the event loop has been stopped. Those captures never fire once
    // the loop stops iterating, so without this their strong self-refs would
    // pin BaseConnection (and the EventLoop itself through BaseConnection::
    // event_loop_) forever, producing the P4 per-connection RSS residue.
    //
    // MUST be called after Stop()/Join() — we walk the underlying ITimer,
    // which is not thread-safe. Safe teardown order: stop loop → join loop
    // thread → ClearFixedProcesses() → ClearAllTimers() → drop owner's
    // shared_ptr<EventLoop>.
    if (timer_) {
        // Walk the full set of live ids and remove them one by one. The
        // underlying wheel will free its slot copies (and their captured
        // closures), which is what lets [self = shared_from_this()] captures
        // finally drop their reference.
        for (uint64_t id : timer_ids_) {
            TimerTask probe;
            probe.SetIdForTest(id);
            timer_->RemoveTimer(probe);  // may be a no-op if already fired
        }
    }
    timer_ids_.clear();
    timers_.clear();
    timer_repeat_.clear();

    // Also drain any posted tasks that were RunInLoop()'d from a different
    // thread. Each posted task may capture shared_ptr<Stream>/<Connection>
    // via [self = shared_from_this()]; if the loop stops before the task
    // is drained, those captures survive and pin the object forever.
    {
        std::lock_guard<std::mutex> lk(tasks_mu_);
        tasks_.clear();
    }
}

uint64_t EventLoop::AddTimer(std::function<void()> cb, uint32_t delay_ms, bool repeat) {
    AssertInLoopThread();
    if (!timer_) {
        LOG_ERROR("EventLoop timer is not initialized. Call Init() first.");
        return 0;
    }
    TimerTask task(cb);
    uint64_t now = UTCTimeMsec();
    uint64_t id = timer_->AddTimer(task, delay_ms, now);
    // P4: only one-shot timers must NOT be retained in EventLoop::timers_,
    // otherwise the callback (which may capture shared_ptr<BaseConnection>
    // via [self]) is held alive past its fire, producing a ~120KB per-
    // connection RSS residue. For repeat timers we still need the cb so
    // we can re-register on each fire, so they stay in timers_.
    timer_ids_.insert(id);
    if (repeat) {
        timer_repeat_[id] = true;
        timers_.emplace(id, task);
    }
    // NOTE: do NOT call Wakeup() here.
    //
    // AddTimer is gated by AssertInLoopThread(), so we are always on the loop
    // thread, which means Wait() is NOT currently blocking on the driver
    // (we are between iterations, inside an event/timer/task callback). The
    // very next Wait() will read MinTime(now) from the timer wheel and pass
    // the freshly-armed deadline straight into driver_->Wait(timeout). No
    // explicit wakeup is needed, and triggering one is actively harmful:
    // the same-thread branch of Wakeup() sets need_immediate_wakeup_, which
    // forces the next Wait() to use timeout=0, defeating the deadline-driven
    // blocking. In the field this manifested as a ~9 s silence in the PTO
    // path: every outgoing packet did RemoveTimer + AddTimer on pto_timer_,
    // and the resulting flag turned every Wait() into a 0-timeout poll, so
    // the wheel was never advanced enough by the driver to actually fire
    // the 88 ms PTO timer when no I/O was pending.
    return id;
}

uint64_t EventLoop::AddTimer(TimerTask& task, uint32_t delay_ms, bool repeat) {
    AssertInLoopThread();
    if (!timer_) {
        LOG_ERROR("EventLoop timer is not initialized. Call Init() first.");
        return 0;
    }
    uint64_t now = UTCTimeMsec();
    uint64_t id = timer_->AddTimer(task, delay_ms, now);
    // Same P4 rationale as the function above: avoid retaining one-shot
    // timers in EventLoop::timers_.
    timer_ids_.insert(id);
    if (repeat) {
        timer_repeat_[id] = true;
        timers_.emplace(id, task);
    }
    // See the long comment in the std::function overload above for why we
    // intentionally do NOT call Wakeup() here.
    return id;
}

bool EventLoop::RemoveTimer(uint64_t timer_id) {
    AssertInLoopThread();
    if (!timer_) {
        LOG_ERROR("EventLoop timer is not initialized. Call Init() first.");
        return false;
    }
    // TimingWheelTimer::RemoveTimer(TimerTask&) only reads task.id_, so we
    // can synthesize a probe task rather than keeping the real TimerTask
    // (and its captured shared_ptr<>-holding tcb_) around in timers_.
    TimerTask probe;
    probe.SetIdForTest(timer_id);
    bool ok = timer_->RemoveTimer(probe);
    timer_ids_.erase(timer_id);
    timers_.erase(timer_id);
    timer_repeat_.erase(timer_id);
    return ok;
}

bool EventLoop::RemoveTimer(TimerTask& task) {
    AssertInLoopThread();
    if (!timer_) {
        LOG_ERROR("EventLoop timer is not initialized. Call Init() first.");
        return false;
    }
    bool ok = timer_->RemoveTimer(task);
    timer_ids_.erase(task.GetId());
    timers_.erase(task.GetId());
    timer_repeat_.erase(task.GetId());
    return ok;
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

std::shared_ptr<ITimer> EventLoop::GetTimer() {
    return timer_;
}

void EventLoop::SetTimerForTest(std::shared_ptr<ITimer> timer) {
    timer_ = timer;
}

void EventLoop::DrainPostedTasks() {
    std::deque<std::function<void()>> q;
    {
        std::lock_guard<std::mutex> lk(tasks_mu_);
        q.swap(tasks_);
    }
    for (auto& fn : q) {
        if (fn) fn();
    }
}

}  // namespace common
}  // namespace quicx
