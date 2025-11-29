#ifndef COMMON_NETWORK_EVENT_LOOP
#define COMMON_NETWORK_EVENT_LOOP

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/network/if_event_driver.h"
#include "common/network/if_event_loop.h"
#include "common/timer/if_timer.h"
#include "common/timer/timer_task.h"

namespace quicx {
namespace common {

class EventLoop: public IEventLoop {
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

    virtual uint64_t AddTimer(std::function<void()> cb, uint32_t delay_ms, bool repeat = false) override;
    virtual uint64_t AddTimer(TimerTask& task, uint32_t delay_ms, bool repeat = false) override;
    virtual bool RemoveTimer(uint64_t timer_id) override;
    virtual bool RemoveTimer(TimerTask& task) override;

    virtual void PostTask(std::function<void()> fn) override;
    virtual void Wakeup() override;

    virtual std::shared_ptr<ITimer> GetTimer() override;

    virtual void SetTimerForTest(std::shared_ptr<ITimer> timer) override;

    // Check if current thread is the loop thread
    virtual bool IsInLoopThread() const override;

    // Execute task immediately if in loop thread, otherwise post it
    virtual void RunInLoop(std::function<void()> task) override;

    // Assert that current thread is loop thread
    virtual void AssertInLoopThread() override;

private:
    void DrainPostedTasks();

    std::unique_ptr<IEventDriver> driver_;
    std::shared_ptr<ITimer> timer_;
    std::vector<Event> events_;

    std::unordered_map<uint64_t, TimerTask> timers_;
    std::unordered_map<uint64_t, bool> timer_repeat_;  // timer id -> repeat
    std::unordered_map<uint32_t, std::weak_ptr<IFdHandler>> fd_to_handler_;

    std::mutex tasks_mu_;
    std::deque<std::function<void()>> tasks_;

    std::vector<std::function<void()>> fixed_processes_;

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
