#ifdef __APPLE__

#ifndef COMMON_NETWORK_MACOS_KQUEUE_EVENT_DRIVER
#define COMMON_NETWORK_MACOS_KQUEUE_EVENT_DRIVER

#include "common/network/if_event_driver.h"

namespace quicx {
namespace common {

// Kqueue event driver implementation for macOS
class KqueueEventDriver:
    public IEventDriver {
public:
    KqueueEventDriver();
    virtual ~KqueueEventDriver();

    // Initialize the kqueue event driver
    virtual bool Init() override;

    // Add a file descriptor to kqueue monitoring
    virtual bool AddFd(int32_t sockfd, int32_t events) override;

    // Remove a file descriptor from kqueue monitoring
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
    // Convert a single kevent to EventType
    EventType ConvertFromKqueueEvent(const struct kevent& kev) const;

    int kqueue_fd_ = -1;
    int32_t wakeup_fd_[2];  // Pipe for wakeup
    int max_events_ = 1024;
};

} // namespace common
} // namespace quicx

#endif // COMMON_NETWORK_MACOS_KQUEUE_EVENT_DRIVER 
#endif // __APPLE__