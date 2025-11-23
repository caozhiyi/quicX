#ifndef COMMON_NETWORK_IF_EVENT_LOOP
#define COMMON_NETWORK_IF_EVENT_LOOP

#include <memory>
#include <cstdint>
#include <functional>

#include "common/timer/if_timer.h"

namespace quicx {
namespace common {

class IFdHandler {
public:
    virtual ~IFdHandler() = default;
    virtual void OnRead(uint32_t fd) = 0;
    virtual void OnWrite(uint32_t fd) = 0;
    virtual void OnError(uint32_t fd) = 0;
    virtual void OnClose(uint32_t fd) = 0;
};

class IEventLoop {
public:
    virtual ~IEventLoop() = default;

    virtual bool Init() = 0;
    virtual int Wait() = 0;

    virtual bool RegisterFd(uint32_t fd, int32_t events, std::shared_ptr<IFdHandler> handler) = 0;
    virtual bool ModifyFd(uint32_t fd, int32_t events) = 0;
    virtual bool RemoveFd(uint32_t fd) = 0;

    virtual void AddFixedProcess(std::function<void()> cb) = 0;

    virtual uint64_t AddTimer(std::function<void()> cb, uint32_t delay_ms, bool repeat = false) = 0;
    virtual uint64_t AddTimer(TimerTask& task, uint32_t delay_ms, bool repeat = false) = 0;
    virtual bool RemoveTimer(uint64_t timer_id) = 0;
    virtual bool RemoveTimer(TimerTask& task) = 0;

    virtual void PostTask(std::function<void()> fn) = 0;
    virtual void Wakeup() = 0;

    virtual std::shared_ptr<ITimer> GetTimer() = 0;

    virtual void SetTimerForTest(std::shared_ptr<ITimer> timer) = 0;

    virtual bool IsInLoopThread() const = 0;
    virtual void RunInLoop(std::function<void()> task) = 0;
    virtual void AssertInLoopThread() = 0;
};

// Factory for default implementation
std::shared_ptr<IEventLoop> MakeEventLoop();

}  // namespace common
}  // namespace quicx

#endif  // COMMON_NETWORK_IF_EVENT_LOOP
