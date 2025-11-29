#include <vector>

#include "common/log/log.h"
#include "common/network/event_loop.h"
#include "common/timer/timer.h"
#include "common/util/time.h"

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

    int n = driver_->Wait(events_, timeout_ms);
    if (n < 0) {
        LOG_ERROR("Event driver wait failed");
        return -1;
    }

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

    // handle fixed processes
    for (auto& cb : fixed_processes_) {
        cb();
    }

    DrainPostedTasks();
    return n;
}

bool EventLoop::RegisterFd(uint32_t fd, int32_t events, std::shared_ptr<IFdHandler> handler) {
    AssertInLoopThread();
    fd_to_handler_[fd] = handler;
    if (!handler) {
        LOG_ERROR("Handler is null for fd %d", fd);
        return false;
    }
    if (!driver_) {
        LOG_ERROR("Event loop driver is not initialized for fd %d", fd);
        return false;
    }
    return driver_->AddFd(fd, events);
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

uint64_t EventLoop::AddTimer(std::function<void()> cb, uint32_t delay_ms, bool repeat) {
    AssertInLoopThread();
    if (!timer_) {
        LOG_ERROR("EventLoop timer is not initialized. Call Init() first.");
        return 0;
    }
    TimerTask task(cb);
    uint64_t now = UTCTimeMsec();
    uint64_t id = timer_->AddTimer(task, delay_ms, now);
    timers_.emplace(id, task);
    if (repeat) {
        timer_repeat_[id] = true;
    }
    Wakeup();
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
    timers_.emplace(id, task);
    if (repeat) {
        timer_repeat_[id] = true;
    }
    Wakeup();
    return id;
}

bool EventLoop::RemoveTimer(uint64_t timer_id) {
    AssertInLoopThread();
    if (!timer_) {
        LOG_ERROR("EventLoop timer is not initialized. Call Init() first.");
        return false;
    }
    auto it = timers_.find(timer_id);
    if (it == timers_.end()) {
        return false;
    }
    bool ok = timer_->RemoveTimer(it->second);
    if (ok) {
        timers_.erase(it);
        timer_repeat_.erase(timer_id);
    }
    return ok;
}

bool EventLoop::RemoveTimer(TimerTask& task) {
    AssertInLoopThread();
    if (!timer_) {
        LOG_ERROR("EventLoop timer is not initialized. Call Init() first.");
        return false;
    }
    auto it = timers_.find(task.GetId());
    if (it == timers_.end()) return false;
    bool ok = timer_->RemoveTimer(it->second);
    timers_.erase(it);
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
