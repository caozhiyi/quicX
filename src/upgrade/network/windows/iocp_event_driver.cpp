#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include "common/log/log.h"
#include "upgrade/network/windows/iocp_event_driver.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

namespace quicx {
namespace upgrade {

IocpEventDriver::IocpEventDriver() {
    // Constructor
}

IocpEventDriver::~IocpEventDriver() {
    if (wakeup_event_ != INVALID_HANDLE_VALUE) {
        CloseHandle(wakeup_event_);
        wakeup_event_ = INVALID_HANDLE_VALUE;
    }
    if (iocp_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(iocp_handle_);
        iocp_handle_ = INVALID_HANDLE_VALUE;
    }
}

bool IocpEventDriver::Init() {
    iocp_handle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (iocp_handle_ == INVALID_HANDLE_VALUE) {
        common::LOG_ERROR("Failed to create IOCP instance");
        return false;
    }
    
    // Create wakeup event
    wakeup_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (wakeup_event_ == INVALID_HANDLE_VALUE) {
        common::LOG_ERROR("Failed to create wakeup event");
        CloseHandle(iocp_handle_);
        iocp_handle_ = INVALID_HANDLE_VALUE;
        return false;
    }
    
    common::LOG_INFO("IOCP event driver initialized with wakeup support");
    return true;
}

bool IocpEventDriver::AddFd(int fd, EventType events) {
    if (iocp_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    SOCKET socket = static_cast<SOCKET>(fd);
    
    // Associate socket with IOCP
    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), iocp_handle_, 
                              reinterpret_cast<ULONG_PTR>(nullptr), 0) == nullptr) {
        common::LOG_ERROR("Failed to associate socket with IOCP");
        return false;
    }

    // Post initial operations based on events
    if (static_cast<int>(events) & static_cast<int>(EventType::READ)) {
        PostReadOperation(socket);
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::WRITE)) {
        PostWriteOperation(socket);
    }

    common::LOG_DEBUG("Added socket %d to IOCP monitoring", fd);
    return true;
}

bool IocpEventDriver::RemoveFd(int fd) {
    SOCKET socket = static_cast<SOCKET>(fd);
    closesocket(socket);
    
    common::LOG_DEBUG("Removed socket %d from IOCP monitoring", fd);
    return true;
}

bool IocpEventDriver::ModifyFd(int fd, EventType events) {
    // Remove and re-add the socket
    RemoveFd(fd);
    return AddFd(fd, events);
}

int IocpEventDriver::Wait(std::vector<Event>& events, int timeout_ms) {
    if (iocp_handle_ == INVALID_HANDLE_VALUE) {
        return -1;
    }

    events.clear();
    events.reserve(max_events_);

    // Wait for multiple completion events
    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    LPOVERLAPPED overlapped;
    DWORD timeout = timeout_ms >= 0 ? static_cast<DWORD>(timeout_ms) : INFINITE;

    // Try to get multiple completion events
    for (int i = 0; i < max_events_; ++i) {
        BOOL result = GetQueuedCompletionStatus(
            iocp_handle_,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            0  // Non-blocking for multiple events
        );

        if (!result) {
            if (overlapped == nullptr) {
                // No more events available
                break;
            }
            // Handle error
            events.push_back(Event{
                static_cast<int>(reinterpret_cast<SOCKET>(completion_key)),
                EventType::ERROR,
            });
        } else {
            // Handle successful completion
            events.push_back(Event{
                static_cast<int>(reinterpret_cast<SOCKET>(completion_key)),
                EventType::READ,  // Simplified - actual implementation would determine type
            });
        }
    }

    // If no events were found, wait for at least one with timeout
    if (events.empty()) {
        BOOL result = GetQueuedCompletionStatus(
            iocp_handle_,
            &bytes_transferred,
            &completion_key,
            &overlapped,
            timeout
        );

        if (result) {
            events.push_back(Event{
                static_cast<int>(reinterpret_cast<SOCKET>(completion_key)),
                EventType::READ,
            });
        }
    }

    return events.size();
}

bool IocpEventDriver::PostReadOperation(SOCKET socket) {
    // Simplified implementation - actual implementation would need proper overlapped structures
    return true;
}

bool IocpEventDriver::PostWriteOperation(SOCKET socket) {
    // Simplified implementation - actual implementation would need proper overlapped structures
    return true;
}

void IocpEventDriver::Wakeup() {
    if (wakeup_event_ != INVALID_HANDLE_VALUE) {
        // Signal the wakeup event
        SetEvent(wakeup_event_);
    }
}

} // namespace upgrade
} // namespace quicx

#endif 