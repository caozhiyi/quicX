#ifndef UPGRADE_NETWORK_LINUX_EPOLL_EVENT_DRIVER_H
#define UPGRADE_NETWORK_LINUX_EPOLL_EVENT_DRIVER_H

#include "upgrade/network/if_event_driver.h"
#include <unordered_map>

namespace quicx {
namespace upgrade {

// Epoll event driver implementation for Linux
class EpollEventDriver : public IEventDriver {
public:
    EpollEventDriver();
    virtual ~EpollEventDriver();

    // Initialize the epoll event driver
    virtual bool Init() override;

    // Add a file descriptor to epoll monitoring
    virtual bool AddFd(int fd, EventType events, void* user_data = nullptr) override;

    // Remove a file descriptor from epoll monitoring
    virtual bool RemoveFd(int fd) override;

    // Modify events for a file descriptor
    virtual bool ModifyFd(int fd, EventType events, void* user_data = nullptr) override;

    // Wait for events with timeout
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;

    // Get the maximum number of events
    virtual int GetMaxEvents() const override { return max_events_; }

private:
    // Convert EventType to epoll events
    uint32_t ConvertToEpollEvents(EventType events) const;
    
    // Convert epoll events to EventType
    EventType ConvertFromEpollEvents(uint32_t epoll_events) const;

    int epoll_fd_ = -1;
    int max_events_ = 1024;
    std::unordered_map<int, void*> fd_user_data_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_LINUX_EPOLL_EVENT_DRIVER_H 