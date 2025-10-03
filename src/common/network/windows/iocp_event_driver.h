#ifdef _WIN32

#ifndef COMMON_NETWORK_WINDOWS_IOCP_EVENT_DRIVER
#define COMMON_NETWORK_WINDOWS_IOCP_EVENT_DRIVER

#include <cstdint>
#include <vector>
#include <unordered_map>
#include "common/network/if_event_driver.h"

namespace quicx {
namespace common {

// IOCP-based event driver implementation for Windows
class IOCPEventDriver: public IEventDriver {
public:
    IOCPEventDriver();
    virtual ~IOCPEventDriver();

    virtual bool Init() override;
    virtual bool AddFd(int32_t sockfd, int32_t events) override;
    virtual bool RemoveFd(int32_t sockfd) override;
    virtual bool ModifyFd(int32_t sockfd, int32_t events) override;
    virtual int Wait(std::vector<Event>& events, int timeout_ms = -1) override;
    virtual int GetMaxEvents() const override { return max_events_; }
    virtual void Wakeup() override;

private:
    struct OverlappedContext {
        OVERLAPPED overlapped;
        int32_t fd;
        EventType type;
        WSABUF wsabuf;
        char byte;
        OverlappedContext(): fd(-1), type(EventType::ET_READ) {
            ZeroMemory(&overlapped, sizeof(overlapped));
            wsabuf.buf = &byte;
            wsabuf.len = 0; // zero-byte recv
            byte = 0;
        }
    };

    bool ArmRead(int32_t fd);
    bool ArmWriteNotif(int32_t fd);

    HANDLE iocp_ = nullptr;
    int max_events_ = 1024;
    ULONG_PTR wake_key_ = static_cast<ULONG_PTR>(-1);

    // track subscriptions
    std::unordered_map<int32_t, int32_t> subscriptions_;
};

}
}

#endif // COMMON_NETWORK_WINDOWS_IOCP_EVENT_DRIVER
#endif // _WIN32


