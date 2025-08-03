#ifdef _WIN32

#ifndef UPGRADE_NETWORK_WINDOWS_IOCP_EVENT_DRIVER_H
#define UPGRADE_NETWORK_WINDOWS_IOCP_EVENT_DRIVER_H

#include <windows.h>
#include <unordered_map>
#include "upgrade/network/if_event_driver.h"

namespace quicx {
namespace upgrade {

// IOCP event driver implementation for Windows
class IocpEventDriver : public IEventDriver {
public:
    IocpEventDriver();
    virtual ~IocpEventDriver();

    // Initialize the IOCP event driver
    virtual bool Init() override;

    // Add a socket to IOCP monitoring
    virtual bool AddFd(int fd, EventType events) override;

    // Remove a socket from IOCP monitoring
    virtual bool RemoveFd(int fd) override;

    // Modify events for a socket
    virtual bool ModifyFd(int fd, EventType events) override;

    // Wait for events with timeout
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;

    // Get the maximum number of events
    virtual int GetMaxEvents() const override { return max_events_; }

    // Wake up from Wait() call
    virtual void Wakeup() override;

private:
    // Post read operation to IOCP
    bool PostReadOperation(SOCKET socket);
    
    // Post write operation to IOCP
    bool PostWriteOperation(SOCKET socket);

    HANDLE iocp_handle_ = INVALID_HANDLE_VALUE;
    HANDLE wakeup_event_ = INVALID_HANDLE_VALUE;  // Event for wakeup
    int max_events_ = 1024;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_NETWORK_WINDOWS_IOCP_EVENT_DRIVER_H 
#endif // _WIN32