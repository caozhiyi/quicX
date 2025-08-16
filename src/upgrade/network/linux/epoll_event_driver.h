#ifdef __linux__

#ifndef UPGRADE_NETWORK_LINUX_EPOLL_EVENT_DRIVER
#define UPGRADE_NETWORK_LINUX_EPOLL_EVENT_DRIVER

#include <cstdint>
#include <unordered_map>
#include "upgrade/network/if_event_driver.h"

namespace quicx {
namespace upgrade {

// Epoll event driver implementation for Linux
class EpollEventDriver:
    public IEventDriver {
public:
    EpollEventDriver();
    virtual ~EpollEventDriver();

    // Initialize the epoll event driver
    virtual bool Init() override;

    // Add a file descriptor to epoll monitoring
    virtual bool AddFd(uint64_t fd, EventType events) override;

    // Remove a file descriptor from epoll monitoring
    virtual bool RemoveFd(uint64_t fd) override;

    // Modify events for a file descriptor
    virtual bool ModifyFd(uint64_t fd, EventType events) override;

    // Wait for events with timeout
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;

    // Get the maximum number of events
    virtual int GetMaxEvents() const override { return max_events_; }

    // Wake up from Wait() call
    virtual void Wakeup() override;

private:
    // Convert EventType to epoll events
    uint32_t ConvertToEpollEvents(EventType events) const;
    
    // Convert epoll events to EventType
    EventType ConvertFromEpollEvents(uint32_t epoll_events) const;

    int epoll_fd_ = -1;
    uint64_t wakeup_fd_[2];  // Pipe for wakeup
    int max_events_ = 1024;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_LINUX_EPOLL_EVENT_DRIVER 
#endif // __linux__