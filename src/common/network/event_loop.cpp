#include <vector>
#include <algorithm>

#include "common/log/log.h"
#include "common/util/time.h"
#include "common/timer/timer.h"
#include "common/network/event_loop.h"

namespace quicx {
namespace common {

bool EventLoop::Init() {
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
    return true;
}

int EventLoop::Wait() {
    uint64_t now = UTCTimeMsec();
    timer_->TimerRun(now);

    int32_t next_ms = timer_->MinTime(now);
    int timeout_ms = next_ms >= 0 ? static_cast<int>(next_ms) : 1000;

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

    DrainPostedTasks();
    return n;
}

bool EventLoop::RegisterFd(uint32_t fd, int32_t events, std::shared_ptr<IFdHandler> handler) {
    fd_to_handler_[fd] = handler;
    if (!handler) {
        LOG_ERROR("Handler is null for fd %d", fd);
        return false;
    }
    return driver_->AddFd(fd, events);
}

bool EventLoop::ModifyFd(uint32_t fd, int32_t events) {
    return driver_->ModifyFd(fd, events);
}

bool EventLoop::RemoveFd(uint32_t fd) {
    fd_to_handler_.erase(fd);
    return driver_->RemoveFd(fd);
}

void EventLoop::AddFixedProcess(std::function<void()> cb) {
    fixed_processes_.push_back(cb);
}

uint64_t EventLoop::AddTimer(std::function<void()> cb, uint32_t delay_ms, bool repeat) {
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

bool EventLoop::RemoveTimer(uint64_t timer_id) {
    auto it = timers_.find(timer_id);
    if (it == timers_.end()) return false;
    bool ok = timer_->RmTimer(it->second);
    timers_.erase(it);
    timer_repeat_.erase(timer_id);
    return ok;
}

void EventLoop::PostTask(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(tasks_mu_);
        tasks_.push_back(std::move(fn));
    }
    Wakeup();
}

void EventLoop::Wakeup() {
    if (driver_) {
        driver_->Wakeup();
    }
}

std::shared_ptr<ITimer> EventLoop::GetTimer() {
    return timer_;
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

} // namespace common
} // namespace quicx
