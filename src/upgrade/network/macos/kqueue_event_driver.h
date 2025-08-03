#ifdef __APPLE__

#ifndef UPGRADE_NETWORK_MACOS_KQUEUE_EVENT_DRIVER_H
#define UPGRADE_NETWORK_MACOS_KQUEUE_EVENT_DRIVER_H

#include <unordered_map>
#include "upgrade/network/if_event_driver.h"

namespace quicx {
namespace upgrade {

// Kqueue event driver implementation for macOS
class KqueueEventDriver:
    public IEventDriver {
public:
    KqueueEventDriver();
    virtual ~KqueueEventDriver();

    // Initialize the kqueue event driver
    virtual bool Init() override;

    // Add a file descriptor to kqueue monitoring
    virtual bool AddFd(int fd, EventType events) override;

    // Remove a file descriptor from kqueue monitoring
    virtual bool RemoveFd(int fd) override;

    // Modify events for a file descriptor
    virtual bool ModifyFd(int fd, EventType events) override;

    // Wait for events with timeout
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;

    // Get the maximum number of events
    virtual int GetMaxEvents() const override { return max_events_; }

    // Wake up from Wait() call
    virtual void Wakeup() override;

private:
    // Convert EventType to kqueue events
    uint32_t ConvertToKqueueEvents(EventType events) const;
    
    // Convert kqueue events to EventType
    EventType ConvertFromKqueueEvents(uint32_t kqueue_events) const;

    int kqueue_fd_ = -1;
    int wakeup_fd_ = -1;  // Pipe for wakeup
    int wakeup_write_fd_ = -1;  // Pipe write end for wakeup
    int max_events_ = 1024;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_MACOS_KQUEUE_EVENT_DRIVER_H 
#endif // __APPLE__