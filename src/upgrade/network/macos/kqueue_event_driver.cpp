#ifdef __APPLE__

#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <unistd.h>
#include <sys/event.h>
#include <sys/types.h>

#include "common/log/log.h"
#include "upgrade/network/macos/kqueue_event_driver.h"

namespace quicx {
namespace upgrade {

KqueueEventDriver::KqueueEventDriver() {
    // Constructor
}

KqueueEventDriver::~KqueueEventDriver() {
    if (wakeup_write_fd_ >= 0) {
        close(wakeup_write_fd_);
        wakeup_write_fd_ = -1;
    }
    if (wakeup_fd_ >= 0) {
        close(wakeup_fd_);
        wakeup_fd_ = -1;
    }
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
    
    // Create pipe for wakeup
    int pipe_fds[2];
    if (pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) < 0) {
        common::LOG_ERROR("Failed to create wakeup pipe: %s", strerror(errno));
        close(kqueue_fd_);
        kqueue_fd_ = -1;
        return false;
    }
    
    wakeup_fd_ = pipe_fds[0];  // Read end
    int write_fd = pipe_fds[1]; // Write end
    
    // Add read end to kqueue for wakeup events
    struct kevent kev;
    EV_SET(&kev, wakeup_fd_, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    
    if (kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr) < 0) {
        common::LOG_ERROR("Failed to add wakeup fd to kqueue: %s", strerror(errno));
        close(write_fd);
        close(wakeup_fd_);
        close(kqueue_fd_);
        wakeup_fd_ = -1;
        kqueue_fd_ = -1;
        return false;
    }
    
    // Store write fd for wakeup calls
    wakeup_write_fd_ = write_fd;
    
    common::LOG_INFO("Kqueue event driver initialized with wakeup support");
    return true;
}

bool KqueueEventDriver::AddFd(int fd, EventType events) {
    if (kqueue_fd_ < 0) {
        return false;
    }

    struct kevent kev;
    uint32_t kqueue_events = ConvertToKqueueEvents(events);
    
    if (kqueue_events & EVFILT_READ) {
        EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr) < 0) {
            common::LOG_ERROR("Failed to add read event for fd %d to kqueue: %s", fd, strerror(errno));
            return false;
        }
    }
    
    if (kqueue_events & EVFILT_WRITE) {
        EV_SET(&kev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
        if (kevent(kqueue_fd_, &kev, 1, nullptr, 0, nullptr) < 0) {
            common::LOG_ERROR("Failed to add write event for fd %d to kqueue: %s", fd, strerror(errno));
            return false;
        }
    }

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

    common::LOG_DEBUG("Removed fd %d from kqueue monitoring", fd);
    return true;
}

bool KqueueEventDriver::ModifyFd(int fd, EventType events) {
    // Remove and re-add the fd
    RemoveFd(fd);
    return AddFd(fd, events);
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
        events.reserve(nfds);  // Reserve space but don't resize yet
        
        for (int i = 0; i < nfds; ++i) {
            // Check if this is a wakeup event
            if (kqueue_events[i].ident == wakeup_fd_) {
                // This is a wakeup event, consume the data
                char buffer[64];
                while (read(wakeup_fd_, buffer, sizeof(buffer)) > 0) {
                    // Consume all wakeup data
                }
                continue;  // Skip adding wakeup events to the result
            }
            
            events.push_back(Event{
                static_cast<int>(kqueue_events[i].ident),
                ConvertFromKqueueEvents(kqueue_events[i].filter)
            });
        }
    }

    return events.size();
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

void KqueueEventDriver::Wakeup() {
    if (wakeup_write_fd_ >= 0) {
        // Write a byte to wake up the kevent
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