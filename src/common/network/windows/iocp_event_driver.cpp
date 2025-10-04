#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <cstring>

#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/network/windows/iocp_event_driver.h"

namespace quicx {
namespace common {

bool IOCPEventDriver::ws_initialized_ = false;

IOCPEventDriver::IOCPEventDriver() {
    if (!ws_initialized_) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            common::LOG_ERROR("WSAStartup failed! error : %d", WSAGetLastError());
        }
        ws_initialized_ = true;
    }
}

IOCPEventDriver::~IOCPEventDriver() {
    if (iocp_ != nullptr) {
        CloseHandle(iocp_);
        iocp_ = nullptr;
    }

    if (ws_initialized_) {
        WSACleanup();
        ws_initialized_ = false;
    }
}

bool IOCPEventDriver::Init() {
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!iocp_) {
        LOG_ERROR("CreateIoCompletionPort failed: %lu", GetLastError());
        return false;
    }
    return true;
}

bool IOCPEventDriver::AddFd(int32_t sockfd, int32_t events) {
    HANDLE h = CreateIoCompletionPort(reinterpret_cast<HANDLE>(static_cast<intptr_t>(sockfd)), iocp_, static_cast<ULONG_PTR>(sockfd), 0);
    if (!h) {
        LOG_ERROR("Associate socket %d with IOCP failed: %lu", sockfd, GetLastError());
        return false;
    }
    subscriptions_[sockfd] = events;
    if (static_cast<int>(events) & static_cast<int>(EventType::ET_READ)) {
        if (!ArmRead(sockfd)) {
            return false;
        }
    }
    // write readiness is armed lazily via Modify when needed
    return true;
}

bool IOCPEventDriver::RemoveFd(int32_t sockfd) {
    subscriptions_.erase(sockfd);
    // No explicit disassociate; closing socket elsewhere will cancel I/O
    return true;
}

bool IOCPEventDriver::ModifyFd(int32_t sockfd, int32_t events) {
    subscriptions_[sockfd] = events;
    if (events & EventType::ET_READ) {
        ArmRead(sockfd);
    }
    // For ET_WRITE, we rely on ideal send backlog change notification
    if (events & EventType::ET_WRITE) {
        ArmWriteNotif(sockfd);
    }
    return true;
}

int IOCPEventDriver::Wait(std::vector<Event>& events, int timeout_ms) {
    events.clear();
    DWORD timeout = (timeout_ms < 0) ? INFINITE : static_cast<DWORD>(timeout_ms);

    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        BOOL ok = GetQueuedCompletionStatus(iocp_, &bytes, &key, &overlapped, timeout);
        if (!ok && overlapped == nullptr) {
            // timeout
            return 0;
        }

        if (key == wake_key_ && overlapped == nullptr) {
            // wakeup event
            return static_cast<int>(events.size());
        }

        if (overlapped == nullptr) {
            continue;
        }

        OverlappedContext* ctx = reinterpret_cast<OverlappedContext*>(overlapped);
        int32_t fd = ctx->fd;
        EventType type = ctx->type;

        // map bytes==0 for TCP read as close
        if (type == EventType::ET_READ && bytes == 0) {
            events.push_back(Event{fd, EventType::ET_CLOSE});
        } else {
            events.push_back(Event{fd, type});
        }

        // re-arm if still subscribed
        auto it = subscriptions_.find(fd);
        if (it != subscriptions_.end()) {
            int32_t want = it->second;
            if (static_cast<int>(want) & static_cast<int>(EventType::ET_READ)) {
                ArmRead(fd);
            }
        }

        // Drain multiple completions in this call up to max_events_
        if (static_cast<int>(events.size()) >= max_events_) {
            break;
        }
        // After first completion, switch to 0-timeout to batch remaining
        timeout = 0;
    }
    return static_cast<int>(events.size());
}

void IOCPEventDriver::Wakeup() {
    PostQueuedCompletionStatus(iocp_, 0, wake_key_, nullptr);
}

bool IOCPEventDriver::ArmRead(int32_t fd) {
    DWORD flags = 0;
    DWORD bytes = 0;
    OverlappedContext* ctx = new OverlappedContext();
    ctx->fd = fd;
    ctx->type = EventType::ET_READ;
    int rc = WSARecv(static_cast<SOCKET>(fd), &ctx->wsabuf, 1, &bytes, &flags, &ctx->overlapped, nullptr);
    if (rc == 0) {
        return true;
    }
    int err = WSAGetLastError();
    if (err == WSA_IO_PENDING) {
        return true;
    }
    LOG_ERROR("WSARecv arm failed fd %d err %d", fd, err);
    delete ctx;
    return false;
}

bool IOCPEventDriver::ArmWriteNotif(int32_t fd) {
#ifndef SIO_IDEAL_SEND_BACKLOG_CHANGE
    // not available; consider immediate ET_WRITE
    return true;
#else
    DWORD bytes = 0;
    OverlappedContext* ctx = new OverlappedContext();
    ctx->fd = fd;
    ctx->type = EventType::ET_WRITE;
    int rc = WSAIoctl(static_cast<SOCKET>(fd), SIO_IDEAL_SEND_BACKLOG_CHANGE, nullptr, 0, nullptr, 0, &bytes, &ctx->overlapped, nullptr);
    if (rc == 0) {
        return true;
    }
    int err = WSAGetLastError();
    if (err == WSA_IO_PENDING) {
        return true;
    }
    LOG_ERROR("WSAIoctl ISB_CHANGE arm failed fd %d err %d", fd, err);
    delete ctx;
    return false;
#endif
}

}
}

#endif // _WIN32


