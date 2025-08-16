#ifdef __linux__

#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/network/linux/epoll_event_driver.h"

namespace quicx {
namespace upgrade {

EpollEventDriver::EpollEventDriver() {
    wakeup_fd_[0] = 0;
    wakeup_fd_[1] = 0;
}

EpollEventDriver::~EpollEventDriver() {
    if (wakeup_fd_[0] > 0) {
        common::Close(wakeup_fd_[0]);
        wakeup_fd_[0] = 0;
    }
    if (wakeup_fd_[1] > 0) {
        common::Close(wakeup_fd_[1]);
        wakeup_fd_[1] = 0;
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
    if (!common::Pipe(wakeup_fd_[0], wakeup_fd_[1])) {
        common::LOG_ERROR("Failed to create wakeup pipe: %s", strerror(errno));
        close(epoll_fd_);
        epoll_fd_ = -1;
        return false;
    }
    
    // Set both ends of the pipe to non-blocking mode
    auto noblock_ret1 = common::SocketNoblocking(wakeup_fd_[0]);
    if (noblock_ret1.errno_ != 0) {
        common::LOG_ERROR("Failed to set wakeup pipe read end non-blocking: %s", strerror(noblock_ret1.errno_));
        common::Close(wakeup_fd_[1]);
        common::Close(wakeup_fd_[0]);
        close(epoll_fd_);
        wakeup_fd_[0] = 0;
        wakeup_fd_[1] = 0;
        epoll_fd_ = -1;
        return false;
    }
    
    auto noblock_ret2 = common::SocketNoblocking(wakeup_fd_[1]);
    if (noblock_ret2.errno_ != 0) {
        common::LOG_ERROR("Failed to set wakeup pipe write end non-blocking: %s", strerror(noblock_ret2.errno_));
        common::Close(wakeup_fd_[1]);
        common::Close(wakeup_fd_[0]);
        close(epoll_fd_);
        wakeup_fd_[0] = 0;
        wakeup_fd_[1] = 0;
        epoll_fd_ = -1;
        return false;
    }
    
    // Add read end to epoll for wakeup events
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = static_cast<int>(wakeup_fd_[0]);  // Store the read fd for identification
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, static_cast<int>(wakeup_fd_[0]), &ev) < 0) {
        common::LOG_ERROR("Failed to add wakeup fd to epoll: %s", strerror(errno));
        common::Close(wakeup_fd_[1]);
        common::Close(wakeup_fd_[0]);
        close(epoll_fd_);
        wakeup_fd_[0] = 0;
        wakeup_fd_[1] = 0;
        epoll_fd_ = -1;
        return false;
    }
    
    common::LOG_INFO("Epoll event driver initialized with wakeup support");
    return true;
}

bool EpollEventDriver::AddFd(uint64_t fd, EventType events) {
    if (epoll_fd_ < 0) {
        return false;
    }

    struct epoll_event ev;
    ev.events = ConvertToEpollEvents(events);
    ev.data.fd = static_cast<int>(fd);

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, static_cast<int>(fd), &ev) < 0) {
        common::LOG_ERROR("Failed to add fd %lu to epoll: %s", fd, strerror(errno));
        return false;
    }

    common::LOG_DEBUG("Added fd %lu to epoll monitoring", fd);
    return true;
}

bool EpollEventDriver::RemoveFd(uint64_t fd) {
    if (epoll_fd_ < 0) {
        return false;
    }

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, static_cast<int>(fd), nullptr) < 0) {
        common::LOG_ERROR("Failed to remove fd %lu from epoll: %s", fd, strerror(errno));
        return false;
    }

    common::LOG_DEBUG("Removed fd %lu from epoll monitoring", fd);
    return true;
}

bool EpollEventDriver::ModifyFd(uint64_t fd, EventType events) {
    if (epoll_fd_ < 0) {
        return false;
    }

    struct epoll_event ev;
    ev.events = ConvertToEpollEvents(events);
    ev.data.fd = static_cast<int>(fd);

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, static_cast<int>(fd), &ev) < 0) {
        common::LOG_ERROR("Failed to modify fd %lu in epoll: %s", fd, strerror(errno));
        return false;
    }

    common::LOG_DEBUG("Modified fd %lu in epoll monitoring", fd);
    return true;
}

int EpollEventDriver::Wait(std::vector<Event>& events, int timeout_ms) {
    if (epoll_fd_ < 0) {
        return -1;
    }

    struct epoll_event epoll_events[max_events_];
    
    common::LOG_DEBUG("EpollEventDriver::Wait: waiting for events with timeout %d ms", timeout_ms);
    int nfds = epoll_wait(epoll_fd_, epoll_events, max_events_, timeout_ms);
    
    if (nfds < 0) {
        if (errno == EINTR) {
            // Interrupted by signal, return 0 events
            common::LOG_DEBUG("EpollEventDriver::Wait: interrupted by signal");
            return 0;
        }
        common::LOG_ERROR("epoll_wait failed: %s", strerror(errno));
        return -1;
    }

    common::LOG_DEBUG("EpollEventDriver::Wait: got %d events", nfds);

    // Clear and resize vector to avoid unnecessary allocations
    events.clear();
    if (nfds > 0) {
        events.reserve(nfds);  // Reserve space but don't resize yet
        
        for (int i = 0; i < nfds; ++i) {
            // Check if this is a wakeup event
            if (epoll_events[i].data.fd == static_cast<int>(wakeup_fd_[0])) {
                // This is a wakeup event, consume the data
                common::LOG_DEBUG("EpollEventDriver::Wait: detected wakeup event");
                char buffer[64];
                ssize_t bytes_read = read(wakeup_fd_[0], buffer, sizeof(buffer));
                common::LOG_DEBUG("EpollEventDriver::Wait: read %zd bytes from wakeup pipe", bytes_read);
                if (bytes_read < 0) {
                    common::LOG_ERROR("Failed to read from wakeup pipe: %s", strerror(errno));
                }
                // Return immediately when wakeup event is detected
                common::LOG_DEBUG("EpollEventDriver::Wait: returning after wakeup");
                continue;
            }
            
            events.push_back(Event{
                static_cast<uint64_t>(epoll_events[i].data.fd),
                ConvertFromEpollEvents(epoll_events[i].events)
            });
        }
    }

    common::LOG_DEBUG("EpollEventDriver::Wait: returning %zu events", events.size());
    return events.size();
}

uint32_t EpollEventDriver::ConvertToEpollEvents(EventType events) const {
    uint32_t epoll_events = 0;
    
    if (static_cast<int>(events) & static_cast<int>(EventType::ET_READ)) {
        epoll_events |= EPOLLIN;
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::ET_WRITE)) {
        epoll_events |= EPOLLOUT;
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::ET_ERROR)) {
        epoll_events |= EPOLLERR;
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::ET_CLOSE)) {
        epoll_events |= EPOLLHUP;
    }

    return epoll_events;
}

EventType EpollEventDriver::ConvertFromEpollEvents(uint32_t epoll_events) const {
    EventType events = static_cast<EventType>(0);
    
    if (epoll_events & EPOLLIN) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::ET_READ));
    }
    if (epoll_events & EPOLLOUT) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::ET_WRITE));
    }
    if (epoll_events & EPOLLERR) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::ET_ERROR));
    }
    if (epoll_events & EPOLLHUP) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::ET_CLOSE));
    }

    return events;
}

void EpollEventDriver::Wakeup() {
    if (wakeup_fd_[1] > 0) {
        // Write a byte to wake up the epoll_wait
        char data = 'w';
        common::LOG_DEBUG("EpollEventDriver::Wakeup: writing to wakeup pipe");
        auto ret = common::Write(wakeup_fd_[1], &data, 1);
        if (ret.return_value_ < 0) {
            common::LOG_ERROR("Failed to write to wakeup pipe: %s", strerror(errno));
        } else {
            common::LOG_DEBUG("EpollEventDriver::Wakeup: successfully wrote %d bytes", ret.return_value_);
        }
    } else {
        common::LOG_ERROR("EpollEventDriver::Wakeup: wakeup pipe not initialized");
    }
}

} // namespace upgrade
} // namespace quicx

#endif 