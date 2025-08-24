#ifdef _WIN32

// Windows headers must be included in the correct order
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <thread>
#include <cstring>
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "quic/udp/action/select/udp_action.h"

namespace quicx {
namespace quic {

UdpAction::UdpAction() {
    active_list_.resize(16);
    memset(pipe_, 0, sizeof(pipe_));
    memset(&pipe_content_, 0, sizeof(pipe_content_));

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        common::LOG_FATAL("WSAStartup failed! error : %d", WSAGetLastError());
        abort();
    }

    if (!common::Pipe(pipe_[0], pipe_[1])) {
        common::LOG_FATAL("pipe init failed! error : %d", WSAGetLastError());
        abort();
    }

    common::SocketNoblocking(pipe_[1]);
    common::SocketNoblocking(pipe_[0]);

    pipe_content_.fd = pipe_[0];
    pipe_content_.events = POLLIN;
}

UdpAction::~UdpAction() {
    WSACleanup();
}

bool UdpAction::AddSocket(int32_t socket) {
    WSAPOLLFD event;
    event.fd = socket;
    event.events = POLLIN;
    select_event_map_[socket] = event;
    return true;
}

void UdpAction::RemoveSocket(int32_t socket) {
    select_event_map_.erase(socket);
}

void UdpAction::Wait(int32_t timeout_ms, std::queue<int32_t>& sockets) {
    active_list_.clear();
    active_list_.push_back(pipe_content_);

    for (auto& kv : select_event_map_) {
        active_list_.push_back(kv.second);
    }

    int32_t ret = WSAPoll(active_list_.data(), active_list_.size(), timeout_ms);
    if (ret < 0) {
        common::LOG_ERROR("WSAPoll failed! error : %d", WSAGetLastError());
        return;
    }

    for (size_t i = 0; i < active_list_.size(); ++i) {
        if (active_list_[i].revents & POLLIN) {
            if (active_list_[i].fd == pipe_[0]) {
                static char buf[8];
                if (recv(pipe_[0], buf, 1, 0) <= 0) {
                    common::LOG_ERROR("recv from pipe failed when weak up.");
                }
                continue;
            }

            sockets.push(active_list_[i].fd);
        }
    }
}

void UdpAction::Wakeup() {
    char buf = 1;
    send(pipe_[1], &buf, 1, 0);
}

}
}

#endif // _WIN32
