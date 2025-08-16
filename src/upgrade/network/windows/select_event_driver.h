#ifdef _WIN32

#ifndef UPGRADE_NETWORK_WINDOWS_SELECT_EVENT_DRIVER
#define UPGRADE_NETWORK_WINDOWS_SELECT_EVENT_DRIVER

#include <vector>
#include <unordered_map>
#include "upgrade/network/if_event_driver.h"

namespace quicx {
namespace upgrade {

// Select-based event driver implementation for Windows (for debugging)
class SelectEventDriver:
    public IEventDriver {
public:
    SelectEventDriver();
    virtual ~SelectEventDriver();

    // Initialize the select event driver
    virtual bool Init() override;

    // Add a socket to select monitoring
    virtual bool AddFd(uint64_t fd, EventType events) override;

    // Remove a socket from select monitoring
    virtual bool RemoveFd(uint64_t fd) override;

    // Modify events for a socket
    virtual bool ModifyFd(uint64_t fd, EventType events) override;

    // Wait for events with timeout
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;

    // Get the maximum number of events
    virtual int GetMaxEvents() const override { return max_events_; }

    // Wake up from Wait() call
    virtual void Wakeup() override;

private:
    // Convert EventType to select events
    int ConvertToSelectEvents(EventType events) const;
    
    // Convert select events to EventType
    EventType ConvertFromSelectEvents(int select_events) const;

    std::unordered_map<uint64_t, EventType> monitored_fds_;  // fd -> events
    uint64_t wakeup_pipe_[2];  // Pipe for wakeup
    int max_events_ = 1024;
    bool initialized_ = false;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_WINDOWS_SELECT_EVENT_DRIVER 
#endif // _WIN32