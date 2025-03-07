#ifdef __linux__

#include <cstdio>
#include <thread>
#include <cstring>
#include <unistd.h>
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/network/epoll/tcp_action.h"

namespace quicx {
namespace upgrade {

TcpAction::TcpAction():
    epoll_handler_(-1) {

    active_list_.resize(16);
    memset(pipe_, 0, sizeof(pipe_));
    memset(&pipe_content_, 0, sizeof(pipe_content_));

    // get EPOLL handle. the param is invalid since LINUX 2.6.8
    epoll_handler_ = epoll_create(1500);
    if (epoll_handler_ == -1) {
        common::LOG_FATAL("EPOLL init failed! error : %d", errno);
        abort();
    }

    if (pipe((int*)pipe_) == -1) {
        common::LOG_FATAL("pipe init failed! error : %d", errno);
        abort();
    }

    common::SocketNoblocking(pipe_[1]);
    common::SocketNoblocking(pipe_[0]);

    pipe_content_.events = EPOLLIN;
    pipe_content_.data.fd = pipe_[0];
    int32_t ret = epoll_ctl(epoll_handler_, EPOLL_CTL_ADD, pipe_[0], &pipe_content_);
    if (ret < 0) {
        common::LOG_FATAL("add pipe handle to EPOLL failed! error :%d", errno);
        abort();
    }
}

TcpAction::~TcpAction() {
    if (epoll_handler_ != -1) {
        close(epoll_handler_);
    }
}

void TcpAction::AddListener(std::shared_ptr<ISocket> socket) {    
    epoll_event ep_event;
    ep_event.events = EPOLLIN;
    ep_event.data.fd = socket;
    int ret = epoll_ctl(epoll_handler_, EPOLL_CTL_ADD, socket, &ep_event);

    if (ret == 0) {
        common::LOG_ERROR("add event to epoll failed! error :%d, sock: %d", errno, socket);
        return false;
    }
    socket_map_[socket->GetSocket()] = socket;
    return true;
}

void TcpAction::AddReceiver(std::shared_ptr<ISocket> socket) {
    epoll_event ep_event;
    ep_event.events = EPOLLIN;
    ep_event.data.fd = socket;
    int ret = epoll_ctl(epoll_handler_, EPOLL_CTL_ADD, socket, &ep_event);

    if (ret == 0) {
        common::LOG_ERROR("add event to epoll failed! error :%d, sock: %d", errno, socket);
        return false;
    }
    socket_map_[socket->GetSocket()] = socket;
    return true;
}

void TcpAction::Wait(uint32_t timeout_ms, std::queue<uint64_t>& sockets) {
    int16_t ret = epoll_wait(epoll_handler_, &*active_list_.begin(), (int)active_list_.size(), timeout_ms);
    if (ret == -1) {
        if (errno == EINTR) {
            return;
        }
        common::LOG_ERROR("epoll wait failed! error:%d", errno);
    } 

    common::LOG_DEBUG("epoll get events! num:%d, thread id: %ld", ret, std::this_thread::get_id());

    for (size_t i = 0; i < ret; i++) {
        if ((uint32_t)active_list_[i].data.fd == pipe_[0]) {
            static char buf[2];
            if (read(pipe_[0], buf, 1) <= 0) {
                common::LOG_ERROR("read from pipe failed when weak up.");
            }
            continue;
        }
        sockets.push(active_list_[i].data.fd);
    }
}

void TcpAction::Wakeup() {
    write(pipe_[1], "1", 1);
}

}
}

#endif