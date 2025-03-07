#ifdef __APPLE__

#include <cstdio>
#include <thread>
#include <cstring>
#include <unistd.h>
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/network/kqueue/tcp_action.h"

namespace quicx {
namespace upgrade {

TcpAction::TcpAction() {
    active_list_.resize(128);
    memset(pipe_, 0, sizeof(pipe_));
    memset(&pipe_content_, 0, sizeof(pipe_content_));

    kqueue_handler_ = kqueue();
    if (kqueue_handler_ == -1) {
        common::LOG_FATAL("kqueue init failed!");
        abort();
    }

    if (pipe(pipe_) == -1) {
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

TcpAction::~TcpAction() {
    close(kqueue_handler_);
    close(pipe_[0]);
    close(pipe_[1]);
}

bool TcpAction::AddListener(std::shared_ptr<ISocket> socket) {
    listener_set_.insert(socket->GetSocket());
    return AddReceiver(socket);
}
bool TcpAction::AddReceiver(std::shared_ptr<ISocket> socket) {
    struct kevent event;
    EV_SET(&event, socket->GetSocket(), EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (kevent(kqueue_handler_, &event, 1, NULL, 0, NULL) == -1) {
        return false;
    }
    socket_map_[socket->GetSocket()] = socket;
    return true;
}

bool TcpAction::AddSender(std::shared_ptr<ISocket> socket) {
    struct kevent event;
    EV_SET(&event, socket->GetSocket(), EVFILT_WRITE, EV_ADD, 0, 0, NULL);
    if (kevent(kqueue_handler_, &event, 1, NULL, 0, NULL) == -1) {
        return false;
    }
    socket_map_[socket->GetSocket()] = socket;
    return true;
}

void TcpAction::Remove(std::shared_ptr<ISocket> socket) {
    struct kevent event;
    EV_SET(&event, socket->GetSocket(), EVFILT_READ|EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(kqueue_handler_, &event, 1, NULL, 0, NULL);
    socket_map_.erase(socket->GetSocket());
    listener_set_.erase(socket->GetSocket());
}

void TcpAction::Wait(uint32_t timeout_ms) {
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
        auto it = socket_map_.find(active_list_[i].ident);
        if (it == socket_map_.end()) {
            common::LOG_ERROR("can't find socket content");
            continue;
        }
        auto socket = it->second.lock();
        if (!socket) {
            common::LOG_ERROR("socket is already closed");
            continue;
        }
        auto handler = socket->GetHandler();
        if (!handler) {
            common::LOG_ERROR("sockethandler is null");
            continue;
        }
        if (active_list_[i].filter & EVFILT_READ) {
            if (listener_set_.find(socket->GetSocket()) != listener_set_.end()) {
                handler->HandleConnect(socket->GetSocket(), this);

            } else {
                handler->HandleRead(socket);
            }
        }
        if (active_list_[i].filter & EVFILT_WRITE) {
            handler->HandleWrite(socket);
        }
    }
}

void TcpAction::Wakeup() {
    char buf[1] = {'w'};
    write(pipe_[1], buf, 1);
}

}
}

#endif // __APPLE__
