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
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool EpollEventDriver::Init() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        common::LOG_ERROR("Failed to create epoll instance: {}", strerror(errno));
        return false;
    }
    
    common::LOG_INFO("Epoll event driver initialized");
    return true;
}

bool EpollEventDriver::AddFd(int fd, EventType events, void* user_data) {
    if (epoll_fd_ < 0) {
        return false;
    }

    struct epoll_event ev;
    ev.events = ConvertToEpollEvents(events);
    ev.data.ptr = user_data;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        common::LOG_ERROR("Failed to add fd {} to epoll: {}", fd, strerror(errno));
        return false;
    }

    fd_user_data_[fd] = user_data;
    common::LOG_DEBUG("Added fd {} to epoll monitoring", fd);
    return true;
}

bool EpollEventDriver::RemoveFd(int fd) {
    if (epoll_fd_ < 0) {
        return false;
    }

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        common::LOG_ERROR("Failed to remove fd {} from epoll: {}", fd, strerror(errno));
        return false;
    }

    fd_user_data_.erase(fd);
    common::LOG_DEBUG("Removed fd {} from epoll monitoring", fd);
    return true;
}

bool EpollEventDriver::ModifyFd(int fd, EventType events, void* user_data) {
    if (epoll_fd_ < 0) {
        return false;
    }

    struct epoll_event ev;
    ev.events = ConvertToEpollEvents(events);
    ev.data.ptr = user_data;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        common::LOG_ERROR("Failed to modify fd {} in epoll: {}", fd, strerror(errno));
        return false;
    }

    fd_user_data_[fd] = user_data;
    common::LOG_DEBUG("Modified fd {} in epoll monitoring", fd);
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
        common::LOG_ERROR("epoll_wait failed: {}", strerror(errno));
        return -1;
    }

    // Clear and resize vector to avoid unnecessary allocations
    events.clear();
    if (nfds > 0) {
        events.resize(nfds);
        
        for (int i = 0; i < nfds; ++i) {
            events[i].fd = static_cast<int>(reinterpret_cast<intptr_t>(epoll_events[i].data.ptr));
            events[i].type = ConvertFromEpollEvents(epoll_events[i].events);
            events[i].user_data = epoll_events[i].data.ptr;
        }
    }

    return nfds;
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

} // namespace upgrade
} // namespace quicx

#endif 