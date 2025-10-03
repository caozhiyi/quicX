#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/network/if_event_driver.h"
#include "upgrade/server/connection_handler.h"

namespace quicx {
namespace upgrade {


void ConnectionHandler::OnRead(uint32_t fd) {
    while (true) {
        common::Address client_addr;
        auto ret = common::Accept(fd, client_addr);

        // No more pending connections (EAGAIN/EWOULDBLOCK/ECONNABORTED mapped to errno_ == 0 and return_value_ == -1)
        if (ret.return_value_ < 0 && ret.errno_ == 0) {
            break;
        }

        // Real error
        if (ret.return_value_ < 0) {
            if (ret.errno_ == EINTR) {
                continue;
            }
            common::LOG_ERROR("Failed to accept connection. errno: %d", ret.errno_);
            break;
        }

        uint64_t client_fd = ret.return_value_;

        // Set non-blocking
        auto noblock_ret = common::SocketNoblocking(client_fd);
        if (noblock_ret.errno_ != 0) {
            common::LOG_ERROR("Failed to set socket non-blocking: %d", noblock_ret.errno_);
            common::Close(client_fd);
            continue;
        }

        // Add to event driver
        if (!event_loop_->RegisterFd(client_fd, common::EventType::ET_READ, handler_)) {
            common::LOG_ERROR("Failed to add client socket to event driver");
            common::Close(client_fd);
            continue;
        }

        // Notify handler
        handler_->OnConnect(client_fd);

        common::LOG_INFO("New connection from %s:%d", client_addr.GetIp().c_str(), client_addr.GetPort());
    }
}

void ConnectionHandler::OnWrite(uint32_t fd) {
    common::LOG_ERROR("OnWrite should not be called for listener %d", fd);
}

void ConnectionHandler::OnError(uint32_t fd) {
    common::LOG_ERROR("OnError for fd %d", fd);
    common::Close(fd);
    event_loop_->RemoveFd(fd);
}

void ConnectionHandler::OnClose(uint32_t fd) {
    common::Close(fd);
    event_loop_->RemoveFd(fd);
}


} // namespace upgrade
} // namespace quicx