#ifdef _WIN32

#ifndef COMMON_NETWORK_WINDOWS_SELECT_EVENT_DRIVER
#define COMMON_NETWORK_WINDOWS_SELECT_EVENT_DRIVER

#include <vector>
#include <unordered_map>
#include "common/network/if_event_driver.h"

namespace quicx {
namespace common {

// Select-based event driver implementation for Windows (for debugging)
class SelectEventDriver:
    public IEventDriver {
public:
    SelectEventDriver();
    virtual ~SelectEventDriver();

    // Initialize the select event driver
    virtual bool Init() override;

    // Add a socket to select monitoring
    virtual bool AddFd(int32_t sockfd, int32_t events) override;

    // Remove a socket from select monitoring
    virtual bool RemoveFd(int32_t sockfd) override;

    // Modify events for a socket
    virtual bool ModifyFd(int32_t sockfd, int32_t events) override;

    // Wait for events with timeout
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;

    // Get the maximum number of events
    virtual int GetMaxEvents() const override { return max_events_; }

    // Wake up from Wait() call
    virtual void Wakeup() override;

private:
    // Convert EventType to select events
    int ConvertToSelectEvents(int32_t events) const;
    
    // Convert select events to EventType
    int32_t ConvertFromSelectEvents(int select_events) const;

    std::unordered_map<int32_t, int32_t> monitored_fds_;  // fd -> events
    int32_t wakeup_fd_[2];  // Pipe for wakeup
    int max_events_ = 1024;
    bool initialized_ = false;
    static bool ws_initialized_;
};

} // namespace common
} // namespace quicx

#endif // COMMON_NETWORK_WINDOWS_SELECT_EVENT_DRIVER 
#endif // _WIN32