#ifdef _WIN32

#include "upgrade/network/windows/iocp_event_driver.h"
#include "common/log/log.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

namespace quicx {
namespace upgrade {

IocpEventDriver::IocpEventDriver() {
    // Constructor
}

IocpEventDriver::~IocpEventDriver() {
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
    
    common::LOG_INFO("IOCP event driver initialized");
    return true;
}

bool IocpEventDriver::AddFd(int fd, EventType events, void* user_data) {
    if (iocp_handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    SOCKET socket = static_cast<SOCKET>(fd);
    
    // Associate socket with IOCP
    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(socket), iocp_handle_, 
                              reinterpret_cast<ULONG_PTR>(user_data), 0) == nullptr) {
        common::LOG_ERROR("Failed to associate socket with IOCP");
        return false;
    }

    socket_user_data_[socket] = user_data;

    // Post initial operations based on events
    if (static_cast<int>(events) & static_cast<int>(EventType::READ)) {
        PostReadOperation(socket, user_data);
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::WRITE)) {
        PostWriteOperation(socket, user_data);
    }

    common::LOG_DEBUG("Added socket {} to IOCP monitoring", fd);
    return true;
}

bool IocpEventDriver::RemoveFd(int fd) {
    SOCKET socket = static_cast<SOCKET>(fd);
    socket_user_data_.erase(socket);
    closesocket(socket);
    
    common::LOG_DEBUG("Removed socket {} from IOCP monitoring", fd);
    return true;
}

bool IocpEventDriver::ModifyFd(int fd, EventType events, void* user_data) {
    // Remove and re-add the socket
    RemoveFd(fd);
    return AddFd(fd, events, user_data);
}

int IocpEventDriver::Wait(std::vector<Event>& events, int timeout_ms) {
    if (iocp_handle_ == INVALID_HANDLE_VALUE) {
        return -1;
    }

    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    LPOVERLAPPED overlapped;
    DWORD timeout = timeout_ms >= 0 ? static_cast<DWORD>(timeout_ms) : INFINITE;

    BOOL result = GetQueuedCompletionStatus(
        iocp_handle_,
        &bytes_transferred,
        &completion_key,
        &overlapped,
        timeout
    );

    if (!result) {
        if (overlapped == nullptr) {
            // Timeout or error
            return 0;
        }
        // Handle error
        events.clear();
        events.resize(1);
        events[0].fd = static_cast<int>(reinterpret_cast<SOCKET>(completion_key));
        events[0].type = EventType::ERROR;
        events[0].user_data = reinterpret_cast<void*>(completion_key);
        return 1;
    }

    // Handle successful completion
    events.clear();
    events.resize(1);
    events[0].fd = static_cast<int>(reinterpret_cast<SOCKET>(completion_key));
    events[0].user_data = reinterpret_cast<void*>(completion_key);
    
    // Determine event type based on overlapped operation
    // This is a simplified implementation
    events[0].type = EventType::READ;
    
    return 1;
}

bool IocpEventDriver::PostReadOperation(SOCKET socket, void* user_data) {
    // Simplified implementation - actual implementation would need proper overlapped structures
    return true;
}

bool IocpEventDriver::PostWriteOperation(SOCKET socket, void* user_data) {
    // Simplified implementation - actual implementation would need proper overlapped structures
    return true;
}

} // namespace upgrade
} // namespace quicx

#endif 