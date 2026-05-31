#ifdef __linux__

#ifndef COMMON_NETWORK_LINUX_EPOLL_EVENT_DRIVER
#define COMMON_NETWORK_LINUX_EPOLL_EVENT_DRIVER

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>

#include "common/network/if_event_driver.h"

namespace quicx {
namespace common {

// Epoll event driver implementation for Linux
class EpollEventDriver:
    public IEventDriver {
public:
    EpollEventDriver();
    virtual ~EpollEventDriver();

    // Initialize the epoll event driver
    virtual bool Init() override;

    // Add a file descriptor to epoll monitoring
    virtual bool AddFd(int32_t sockfd, int32_t events) override;

    // Remove a file descriptor from epoll monitoring
    virtual bool RemoveFd(int32_t sockfd) override;

    // Modify events for a file descriptor
    virtual bool ModifyFd(int32_t sockfd, int32_t events) override;

    // Wait for events with timeout
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;

    // Get the maximum number of events
    virtual int GetMaxEvents() const override { return max_events_; }

    // Wake up from Wait() call
    virtual void Wakeup() override;

private:
    // Convert EventType to epoll events
    uint32_t ConvertToEpollEvents(int32_t events) const;
    
    // Convert epoll events to EventType
    EventType ConvertFromEpollEvents(uint32_t epoll_events) const;

    int epoll_fd_ = -1;
    int32_t wakeup_fd_[2];  // Pipe for wakeup
    int max_events_ = 1024;

    // Reusable scratch buffer for epoll_wait() output. Allocating a fresh
    // 1024-entry vector on every Wait() call shows up as the dominant CPU
    // hotspot in profiling (memset + malloc/free of ~24KB per loop iter),
    // so we keep one as a member and resize lazily.
    std::vector<struct epoll_event> epoll_events_scratch_;
};

} // namespace common
} // namespace quicx

#endif // COMMON_NETWORK_LINUX_EPOLL_EVENT_DRIVER 
#endif // __linux__