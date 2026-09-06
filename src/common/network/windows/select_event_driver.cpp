#ifdef _WIN32

// Windows headers must be included in the correct order: winsock2.h before windows.h and mswsock.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Winsock's default FD_SETSIZE is 64: fd_set is a plain SOCKET array and
// FD_SET does NOT bounds-check, so monitoring >64 sockets silently corrupts
// the stack. The build defines this project-wide (CMake -DFD_SETSIZE / Bazel
// copts); this #ifndef is a safety net for builds that miss the global
// define and must stay in sync with it.
#ifndef FD_SETSIZE
#define FD_SETSIZE 1024
#endif
#include <mswsock.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/network/windows/select_event_driver.h"

namespace quicx {
namespace common {

std::atomic<int> SelectEventDriver::ws_refcount_(0);

SelectEventDriver::SelectEventDriver() {
    wakeup_fd_[0] = -1;
    wakeup_fd_[1] = -1;

    // WSAStartup/WSACleanup are process-global; keep our own refcount so
    // only the LAST destroyed driver tears winsock down. (The old static
    // bool skipped WSAStartup for the 2nd+ driver but let the 1st one's
    // destructor call WSACleanup while the others were still using sockets.)
    if (ws_refcount_.fetch_add(1) == 0) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            LOG_ERROR("WSAStartup failed! error : %d", WSAGetLastError());
            ws_refcount_.fetch_sub(1);
        }
    }
}

SelectEventDriver::~SelectEventDriver() {
    // Wakeup sockets are initialized to -1 (not 0); the old "!= 0" check
    // always held and closed socket (-1) — harmless but never validated.
    if (wakeup_fd_[0] != -1) {
        closesocket(static_cast<SOCKET>(wakeup_fd_[0]));
    }
    if (wakeup_fd_[1] != -1) {
        closesocket(static_cast<SOCKET>(wakeup_fd_[1]));
    }

    if (ws_refcount_.fetch_sub(1) == 1) {
        WSACleanup();
    }
}

bool SelectEventDriver::Init() {
    // Create wakeup pipe
    if (!Pipe(wakeup_fd_[0], wakeup_fd_[1])) {
        LOG_ERROR("Failed to create wakeup pipe");
        return false;
    }

    // Set non-blocking
    SocketNoblocking(wakeup_fd_[0]);
    SocketNoblocking(wakeup_fd_[1]);

    // Add wakeup pipe to monitoring
    monitored_fds_[wakeup_fd_[0]] = EventType::ET_READ;

    initialized_ = true;
    LOG_INFO("Select event driver initialized with wakeup support");
    return true;
}

bool SelectEventDriver::AddFd(int32_t sockfd, int32_t events) {
    if (!initialized_) {
        return false;
    }

    monitored_fds_[sockfd] = events;
    LOG_DEBUG("Added socket %d to select monitoring", sockfd);
    return true;
}

bool SelectEventDriver::RemoveFd(int32_t sockfd) {
    if (!initialized_) {
        return false;
    }

    monitored_fds_.erase(sockfd);
    LOG_DEBUG("Removed socket %d from select monitoring", sockfd);
    return true;
}

bool SelectEventDriver::ModifyFd(int32_t sockfd, int32_t events) {
    if (!initialized_) {
        return false;
    }

    monitored_fds_[sockfd] = events;
    LOG_DEBUG("Modified socket %d in select monitoring", sockfd);
    return true;
}

int SelectEventDriver::Wait(std::vector<Event>& events, int timeout_ms) {
    if (!initialized_) {
        return -1;
    }

    events.clear();

    // Prepare fd_sets for select
    fd_set readfds, writefds, exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    int maxfd = -1;

    // Add all monitored fds to appropriate sets
    for (const auto& pair : monitored_fds_) {
        int32_t fd = pair.first;
        int32_t events = pair.second;

        if (fd > maxfd) {
            maxfd = static_cast<int>(fd);
        }

        if (events & EventType::ET_READ) {
            FD_SET(static_cast<int>(fd), &readfds);
        }
        if (events & EventType::ET_WRITE) {
            FD_SET(static_cast<int>(fd), &writefds);
        }
        if (events & EventType::ET_ERROR) {
            FD_SET(static_cast<int>(fd), &exceptfds);
        }
    }

    // Prepare timeout
    struct timeval timeout;
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
    }

    // Call select
    int result = select(maxfd + 1, &readfds, &writefds, &exceptfds, timeout_ms >= 0 ? &timeout : nullptr);

    if (result < 0) {
        LOG_ERROR("select failed: %d", WSAGetLastError());
        return -1;
    }

    if (result == 0) {
        // Timeout
        return 0;
    }

    // Process results
    for (const auto& pair : monitored_fds_) {
        int32_t fd = pair.first;
        int32_t monitored_events = pair.second;

        // Skip wakeup pipe events
        if (fd == wakeup_fd_[0]) {
            if (FD_ISSET(static_cast<int>(fd), &readfds)) {
                // Consume wakeup data
                char buffer[64];
                while (recv(static_cast<int>(fd), buffer, sizeof(buffer), 0) > 0) {
                    // Consume all wakeup data
                }
            }
            continue;
        }

        // Check for events - use independent if statements to detect multiple events on same fd
        bool has_event = false;

        if (FD_ISSET(static_cast<int>(fd), &readfds)) {
            events.push_back(Event{fd, EventType::ET_READ});
            has_event = true;
        }
        if (FD_ISSET(static_cast<int>(fd), &writefds)) {
            events.push_back(Event{fd, EventType::ET_WRITE});
            has_event = true;
        }
        if (FD_ISSET(static_cast<int>(fd), &exceptfds)) {
            events.push_back(Event{fd, EventType::ET_ERROR});
            has_event = true;
        }
    }

    return events.size();
}

void SelectEventDriver::Wakeup() {
    if (wakeup_fd_[1] != 0) {
        char data = 1;
        send(static_cast<int>(wakeup_fd_[1]), &data, 1, 0);
    }
}

int SelectEventDriver::ConvertToSelectEvents(int32_t events) const {
    int select_events = 0;

    if (events & EventType::ET_READ) {
        select_events |= FD_READ;
    }
    if (events & EventType::ET_WRITE) {
        select_events |= FD_WRITE;
    }
    if (events & EventType::ET_ERROR) {
        select_events |= FD_OOB;
    }

    return select_events;
}

int32_t SelectEventDriver::ConvertFromSelectEvents(int select_events) const {
    int events_int = 0;

    if (select_events & FD_READ) {
        events_int |= EventType::ET_READ;
    }
    if (select_events & FD_WRITE) {
        events_int |= EventType::ET_WRITE;
    }
    if (select_events & FD_OOB) {
        events_int |= EventType::ET_ERROR;
    }

    return events_int;
}

}  // namespace common
}  // namespace quicx

#endif  // _WIN32