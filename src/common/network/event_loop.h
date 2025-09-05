#ifndef COMMON_NETWORK_EVENT_LOOP
#define COMMON_NETWORK_EVENT_LOOP

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <deque>
#include <mutex>

#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"
#include "common/network/if_event_loop.h"
#include "common/network/if_event_driver.h"

namespace quicx {
namespace common {

class EventLoop:
    public IEventLoop {
public:
    EventLoop() = default;
    ~EventLoop() = default;

    virtual bool Init() override;
    // Run one iteration: run timers, wait for I/O up to next timer, dispatch callbacks
    // Returns number of I/O events dispatched
    virtual int Wait() override;

    virtual bool RegisterFd(int fd, EventType events, IFdHandler* handler) override;
    virtual bool ModifyFd(int fd, EventType events) override;
    virtual bool RemoveFd(int fd) override;

    virtual uint64_t AddTimer(std::function<void()> cb, uint32_t delay_ms, bool repeat = false) override;
    virtual bool RemoveTimer(uint64_t timer_id) override;

    virtual void PostTask(std::function<void()> fn) override;
    virtual void Wakeup() override;

private:
    void DrainPostedTasks();

    std::unique_ptr<IEventDriver> driver_;
    std::shared_ptr<ITimer> timer_;

    std::unordered_map<int, IFdHandler*> fd_to_handler_;
    std::unordered_map<uint64_t, TimerTask> timers_;
    std::unordered_map<uint64_t, bool> timer_repeat_; // timer id -> repeat

    std::mutex tasks_mu_;
    std::deque<std::function<void()>> tasks_;
};

}
}

#endif // COMMON_NETWORK_EVENT_LOOP


