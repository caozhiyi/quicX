#include <vector>
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
    return true;
}

int EventLoop::Wait() {
    std::vector<Event> events;
    events.reserve(driver_->GetMaxEvents());

    uint64_t now = UTCTimeMsec();
    timer_->TimerRun(now);

    int32_t next_ms = timer_->MinTime(now);
    int timeout_ms = next_ms >= 0 ? static_cast<int>(next_ms) : 1000;

    int n = driver_->Wait(events, timeout_ms);
    if (n < 0) {
        LOG_ERROR("Event driver wait failed");
        return -1;
    }

    // handle events
    for (auto& ev : events) {
        auto it = fd_to_handler_.find(ev.fd);
        if (it == fd_to_handler_.end()) {
            continue;
        }
        IFdHandler* handler = it->second;
        switch (ev.type) {
        case EventType::ET_READ:
            if (handler) handler->OnRead(ev.fd);
            break;
        case EventType::ET_WRITE:
            if (handler) handler->OnWrite(ev.fd);
            break;
        case EventType::ET_ERROR:
            if (handler) handler->OnError(ev.fd);
            break;
        case EventType::ET_CLOSE:
            if (handler) handler->OnClose(ev.fd);
            break;
        }
    }

    DrainPostedTasks();
    return n;
}

bool EventLoop::RegisterFd(int fd, EventType events, IFdHandler* handler) {
    fd_to_handler_[fd] = handler;
    return driver_->AddFd(fd, events);
}

bool EventLoop::ModifyFd(int fd, EventType events) {
    return driver_->ModifyFd(fd, events);
}

bool EventLoop::RemoveFd(int fd) {
    fd_to_handler_.erase(fd);
    return driver_->RemoveFd(fd);
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
