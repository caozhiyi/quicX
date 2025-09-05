#ifndef COMMON_NETWORK_IF_EVENT_LOOP
#define COMMON_NETWORK_IF_EVENT_LOOP

#include <cstdint>
#include <functional>

#include "common/network/if_event_driver.h"

namespace quicx {
namespace common {

class IFdHandler {
public:
    virtual ~IFdHandler() = default;
    virtual void OnRead(int fd) = 0;
    virtual void OnWrite(int fd) = 0;
    virtual void OnError(int fd) = 0;
    virtual void OnClose(int fd) = 0;
};  

class IEventLoop {
public:
    virtual ~IEventLoop() = default;

    virtual bool Init() = 0;
    virtual int Wait() = 0;

    virtual bool RegisterFd(int fd, EventType events, IFdHandler* handler) = 0;
    virtual bool ModifyFd(int fd, EventType events) = 0;
    virtual bool RemoveFd(int fd) = 0;

    virtual uint64_t AddTimer(std::function<void()> cb, uint32_t delay_ms, bool repeat = false) = 0;
    virtual bool RemoveTimer(uint64_t timer_id) = 0;

    virtual void PostTask(std::function<void()> fn) = 0;
    virtual void Wakeup() = 0;
};

// Factory for default implementation
std::unique_ptr<IEventLoop> MakeEventLoop();

}
}

#endif // COMMON_NETWORK_IF_EVENT_LOOP


