#ifndef UPGRADE_NETWORK_MACOS_KQUEUE_EVENT_DRIVER_H
#define UPGRADE_NETWORK_MACOS_KQUEUE_EVENT_DRIVER_H

#include "upgrade/network/if_event_driver.h"
#include <unordered_map>

namespace quicx {
namespace upgrade {

// Kqueue event driver implementation for macOS
class KqueueEventDriver : public IEventDriver {
public:
    KqueueEventDriver();
    virtual ~KqueueEventDriver();

    // Initialize the kqueue event driver
    virtual bool Init() override;

    // Add a file descriptor to kqueue monitoring
    virtual bool AddFd(int fd, EventType events, void* user_data = nullptr) override;

    // Remove a file descriptor from kqueue monitoring
    virtual bool RemoveFd(int fd) override;

    // Modify events for a file descriptor
    virtual bool ModifyFd(int fd, EventType events, void* user_data = nullptr) override;

    // Wait for events with timeout
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;

    // Get the maximum number of events
    virtual int GetMaxEvents() const override { return max_events_; }

private:
    // Convert EventType to kqueue events
    uint32_t ConvertToKqueueEvents(EventType events) const;
    
    // Convert kqueue events to EventType
    EventType ConvertFromKqueueEvents(uint32_t kqueue_events) const;

    int kqueue_fd_ = -1;
    int max_events_ = 1024;
    std::unordered_map<int, void*> fd_user_data_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_MACOS_KQUEUE_EVENT_DRIVER_H 