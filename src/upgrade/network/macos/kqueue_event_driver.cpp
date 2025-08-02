#ifdef __APPLE__

#include "upgrade/network/macos/kqueue_event_driver.h"
#include "common/log/log.h"
#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace quicx {
namespace upgrade {

KqueueEventDriver::KqueueEventDriver() {
    // Constructor
}

KqueueEventDriver::~KqueueEventDriver() {
    if (kqueue_fd_ >= 0) {
        close(kqueue_fd_);
        kqueue_fd_ = -1;
    }
}

bool KqueueEventDriver::Init() {
    kqueue_fd_ = kqueue();
    if (kqueue_fd_ < 0) {
        common::LOG_ERROR("Failed to create kqueue instance: %s", strerror(errno));
        return false;
    }
    
    common::LOG_INFO("Kqueue event driver initialized");
    return true;
}

bool KqueueEventDriver::AddFd(int fd, EventType events, void* user_data) {
    if (kqueue_fd_ < 0) {
        return false;
    }

    struct kevent kev;
    uint32_t kqueue_events = ConvertToKqueueEvents(events);
    
    if (kqueue_events & EVFILT_READ) {
        EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, user_data);
        if (kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr) < 0) {
            common::LOG_ERROR("Failed to add read event for fd %d to kqueue: %s", fd, strerror(errno));
            return false;
        }
    }
    
    if (kqueue_events & EVFILT_WRITE) {
        EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, user_data);
        if (kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr) < 0) {
            common::LOG_ERROR("Failed to add write event for fd %d to kqueue: %s", fd, strerror(errno));
            return false;
        }
    }

    fd_user_data_[fd] = user_data;
    common::LOG_DEBUG("Added fd %d to kqueue monitoring", fd);
    return true;
}

bool KqueueEventDriver::RemoveFd(int fd) {
    if (kqueue_fd_ < 0) {
        return false;
    }

    struct kevent kev;
    
    // Remove read events
    EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr);
    
    // Remove write events
    EV_SET(&kev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr);

    fd_user_data_.erase(fd);
    common::LOG_DEBUG("Removed fd %d from kqueue monitoring", fd);
    return true;
}

bool KqueueEventDriver::ModifyFd(int fd, EventType events, void* user_data) {
    // Remove and re-add the fd
    RemoveFd(fd);
    return AddFd(fd, events, user_data);
}

int KqueueEventDriver::Wait(std::vector<Event>& events, int timeout_ms) {
    if (kqueue_fd_ < 0) {
        return -1;
    }

    struct kevent kqueue_events[max_events_];
    struct timespec timeout;
    
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_nsec = (timeout_ms % 1000) * 1000000;
    }
    
    int nfds = kevent(kqueue_fd_, nullptr, 0, kqueue_events, max_events_, 
                     timeout_ms >= 0 ? &timeout : nullptr);
    
    if (nfds < 0) {
        if (errno == EINTR) {
            // Interrupted by signal, return 0 events
            return 0;
        }
        common::LOG_ERROR("kevent failed: %s", strerror(errno));
        return -1;
    }

    // Clear and resize vector to avoid unnecessary allocations
    events.clear();
    if (nfds > 0) {
        events.resize(nfds);
        
        for (int i = 0; i < nfds; ++i) {
            events[i].fd = static_cast<int>(kqueue_events[i].ident);
            events[i].type = ConvertFromKqueueEvents(kqueue_events[i].filter);
            events[i].user_data = kqueue_events[i].udata;
        }
    }

    return nfds;
}

uint32_t KqueueEventDriver::ConvertToKqueueEvents(EventType events) const {
    uint32_t kqueue_events = 0;
    
    if (static_cast<int>(events) & static_cast<int>(EventType::READ)) {
        kqueue_events |= EVFILT_READ;
    }
    if (static_cast<int>(events) & static_cast<int>(EventType::WRITE)) {
        kqueue_events |= EVFILT_WRITE;
    }

    return kqueue_events;
}

EventType KqueueEventDriver::ConvertFromKqueueEvents(uint32_t kqueue_events) const {
    EventType events = static_cast<EventType>(0);
    
    if (kqueue_events & EVFILT_READ) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::READ));
    }
    if (kqueue_events & EVFILT_WRITE) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::WRITE));
    }
    if (kqueue_events & EV_ERROR) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::ERROR));
    }
    if (kqueue_events & EV_EOF) {
        events = static_cast<EventType>(static_cast<int>(events) | static_cast<int>(EventType::CLOSE));
    }

    return events;
}

} // namespace upgrade
} // namespace quicx

#endif 