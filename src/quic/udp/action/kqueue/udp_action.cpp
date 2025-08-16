#ifdef __APPLE__

#include <cstdio>
#include <thread>
#include <cstring>
#include <unistd.h>
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "quic/udp/action/kqueue/udp_action.h"

namespace quicx {
namespace quic {

UdpAction::UdpAction() {
    active_list_.resize(16);
    memset(pipe_, 0, sizeof(pipe_));
    memset(&pipe_content_, 0, sizeof(pipe_content_));

    kqueue_handler_ = kqueue();
    if (kqueue_handler_ == -1) {
        common::LOG_FATAL("kqueue init failed!");
        abort();
    }

    if (!common::Pipe(pipe_[0], pipe_[1])) {
        common::LOG_FATAL("pipe init failed!");
        abort();
    }

    common::SocketNoblocking(pipe_[1]);
    common::SocketNoblocking(pipe_[0]);

    EV_SET(&pipe_content_, pipe_[0], EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kqueue_handler_, &pipe_content_, 1, NULL, 0, NULL) == -1) {
        common::LOG_FATAL("add pipe to kqueue failed!");
        abort();
    }
}

UdpAction::~UdpAction() {
    close(kqueue_handler_);
    close(pipe_[0]);
    close(pipe_[1]);
}

bool UdpAction::AddSocket(uint64_t socket) {
    struct kevent event;
    EV_SET(&event, socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kqueue_handler_, &event, 1, NULL, 0, NULL) == -1) {
        return false;
    }
    kqueue_event_map_[socket] = event;
    return true;
}

void UdpAction::RemoveSocket(uint64_t socket) {
    auto it = kqueue_event_map_.find(socket);
    if (it != kqueue_event_map_.end()) {
        struct kevent event;
        EV_SET(&event, socket, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(kqueue_handler_, &event, 1, NULL, 0, NULL);
        kqueue_event_map_.erase(it);
    }
}

void UdpAction::Wait(int32_t timeout_ms, std::queue<uint64_t>& sockets) {
    struct timespec timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_nsec = (timeout_ms % 1000) * 1000000;

    int num_events = kevent(kqueue_handler_, NULL, 0, active_list_.data(), active_list_.size(), &timeout);
    if (num_events == -1) {
        return;
    }

    for (int i = 0; i < num_events; i++) {
        if (active_list_[i].ident == (uintptr_t)pipe_[0]) {
            char buf[1024];
            read(pipe_[0], buf, sizeof(buf));
            continue;
        }
        sockets.push(active_list_[i].ident);
    }
}

void UdpAction::Wakeup() {
    char buf[1] = {'w'};
    write(pipe_[1], buf, 1);
}

}
}

#endif // __APPLE__
