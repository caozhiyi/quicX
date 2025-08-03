#ifdef __linux__

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

#include "common/log/log.h"
#include "upgrade/network/linux/epoll_event_driver.h"

namespace quicx {
namespace upgrade {

EpollEventDriver::EpollEventDriver() {
    // Constructor
}

EpollEventDriver::~EpollEventDriver() {
    if (wakeup_write_fd_ >= 0) {
        close(wakeup_write_fd_);
        wakeup_write_fd_ = -1;
    }
    if (wakeup_fd_ >= 0) {
        close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool EpollEventDriver::Init() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        common::LOG_ERROR("Failed to create epoll instance: %s", strerror(errno));
        return false;
    }
    
    // Create pipe for wakeup
    int pipe_fds[2];
    if (pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) < 0) {
        common::LOG_ERROR("Failed to create wakeup pipe: %s", strerror(errno));
        close(epoll_fd_);
        epoll_fd_ = -1;
        return false;
    }
    
    wakeup_fd_ = pipe_fds[0];  // Read end
    int write_fd = pipe_fds[1]; // Write end
    
    // Add read end to epoll for wakeup events
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.ptr = nullptr;  // Special marker for wakeup
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev) < 0) {
        common::LOG_ERROR("Failed to add wakeup fd to epoll: %s", strerror(errno));
        close(write_fd);
        close(wakeup_fd_);
        close(epoll_fd_);
        wakeup_fd_ = -1;
        epoll_fd_ = -1;
        return false;
    }
    
    // Store write fd for wakeup calls
    wakeup_write_fd_ = write_fd;
    
    common::LOG_INFO("Epoll event driver initialized with wakeup support");
    return true;
}

bool EpollEventDriver::AddFd(int fd, EventType events) {
    if (epoll_fd_ < 0) {
        return false;
    }

    struct epoll_event ev;
    ev.events = ConvertToEpollEvents(events);
    ev.data.ptr = nullptr;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        common::LOG_ERROR("Failed to add fd %d to epoll: %s", fd, strerror(errno));
        return false;
    }

    common::LOG_DEBUG("Added fd %d to epoll monitoring", fd);
    return true;
}

bool EpollEventDriver::RemoveFd(int fd) {
    if (epoll_fd_ < 0) {
        return false;
    }

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        common::LOG_ERROR("Failed to remove fd %d from epoll: %s", fd, strerror(errno));
        return false;
    }

    common::LOG_DEBUG("Removed fd %d from epoll monitoring", fd);
    return true;
}

bool EpollEventDriver::ModifyFd(int fd, EventType events) {
    if (epoll_fd_ < 0) {
        return false;
    }

    struct epoll_event ev;
    ev.events = ConvertToEpollEvents(events);
    ev.data.fd = fd;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        common::LOG_ERROR("Failed to modify fd %d in epoll: %s", fd, strerror(errno));
        return false;
    }

    common::LOG_DEBUG("Modified fd %d in epoll monitoring", fd);
    return true;
}

int EpollEventDriver::Wait(std::vector<Event>& events, int timeout_ms) {
    if (epoll_fd_ < 0) {
        return -1;
    }

    struct epoll_event epoll_events[max_events_];
    
    int nfds = epoll_wait(epoll_fd_, epoll_events, max_events_, timeout_ms);
    
    if (nfds < 0) {
        if (errno == EINTR) {
            // Interrupted by signal, return 0 events
            return 0;
        }
        common::LOG_ERROR("epoll_wait failed: %s", strerror(errno));
        return -1;
    }

    // Clear and resize vector to avoid unnecessary allocations
    events.clear();
    if (nfds > 0) {
        events.reserve(nfds);  // Reserve space but don't resize yet
        
        for (int i = 0; i < nfds; ++i) {
            // Check if this is a wakeup event
            if (epoll_events[i].data.fd == wakeup_fd_) {
                // This is a wakeup event, consume the data
                char buffer[64];
                while (read(wakeup_fd_, buffer, sizeof(buffer)) > 0) {
                    // Consume all wakeup data
                }
                continue;  // Skip adding wakeup events to the result
            }
            
            events.push_back(Event{
                epoll_events[i].data.fd,
                ConvertFromEpollEvents(epoll_events[i].events)
            });
        }
    }

    return events.size();
}

uint32_t EpollEventDriver::ConvertToEpollEvents(EventType events) const {
    uint32_t epoll_events = 0;
    
    if (static_cast<int>(events) & static_cast<int>(EventType::READ)) {
        epoll_events |= EPOLLIN;
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::WRITE)) {
        epoll_events |= EPOLLOUT;
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::ERROR)) {
        epoll_events |= EPOLLERR;
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::CLOSE)) {
        epoll_events |= EPOLLHUP;
    }

    return epoll_events;
}

EventType EpollEventDriver::ConvertFromEpollEvents(uint32_t epoll_events) const {
    EventType events = static_cast<EventType>(0);
    
    if (epoll_events & EPOLLIN) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::READ));
    }
    if (epoll_events & EPOLLOUT) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::WRITE));
    }
    if (epoll_events & EPOLLERR) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::ERROR));
    }
    if (epoll_events & EPOLLHUP) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::CLOSE));
    }

    return events;
}

void EpollEventDriver::Wakeup() {
    if (wakeup_write_fd_ >= 0) {
        // Write a byte to wake up the epoll_wait
        char data = 'w';
        ssize_t written = write(wakeup_write_fd_, &data, 1);
        if (written < 0) {
            common::LOG_ERROR("Failed to write to wakeup pipe: %s", strerror(errno));
        }
    }
}

} // namespace upgrade
} // namespace quicx

#endif 