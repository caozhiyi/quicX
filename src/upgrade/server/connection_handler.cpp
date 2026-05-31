#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "common/network/if_event_driver.h"
#include "upgrade/server/connection_handler.h"

namespace quicx {
namespace upgrade {


void ConnectionHandler::OnRead(uint32_t fd) {
    auto loop = event_loop_.lock();
    if (!loop) return;
    while (true) {
        common::Address client_addr;
        auto ret = common::Accept(fd, client_addr);

        // No more pending connections (EAGAIN/EWOULDBLOCK/ECONNABORTED mapped to errno_ == 0 and return_value_ == -1)
        if (ret.return_value_ < 0 && ret.error_code_ == 0) {
            break;
        }

        // Real error
        if (ret.return_value_ < 0) {
            if (ret.error_code_ == EINTR) {
                continue;
            }
            LOG_ERROR("Failed to accept connection. errno: %d", ret.error_code_);
            break;
        }

        uint64_t client_fd = ret.return_value_;

        // Set non-blocking
        auto noblock_ret = common::SocketNoblocking(client_fd);
        if (noblock_ret.error_code_ != 0) {
            LOG_ERROR("Failed to set socket non-blocking: %d", noblock_ret.error_code_);
            common::Close(client_fd);
            continue;
        }

        // Register the client fd with the smart handler so its OnRead /
        // OnWrite drive TLS handshake (HTTPS) or protocol detection (HTTP).
        if (!loop->RegisterFd(client_fd, common::EventType::ET_READ, handler_)) {
            LOG_ERROR("Failed to add client socket to event driver");
            common::Close(client_fd);
            continue;
        }

        // Notify smart handler about the new connection so it can allocate
        // per-connection state (SSL object, ConnectionContext, timer).
        handler_->OnConnect(client_fd);

        LOG_INFO("[TLSDBG] accept ok: client_fd=%lu from %s:%d (registered ET_READ, OnConnect done)",
                 (unsigned long)client_fd, client_addr.GetIp().c_str(), client_addr.GetPort());
    }
}

void ConnectionHandler::OnWrite(uint32_t fd) {
    LOG_ERROR("OnWrite should not be called for listener %d", fd);
}

void ConnectionHandler::OnError(uint32_t fd) {
    LOG_ERROR("OnError for fd %d", fd);
    common::Close(fd);
    if (auto loop = event_loop_.lock()) {
        loop->RemoveFd(fd);
    }
}

void ConnectionHandler::OnClose(uint32_t fd) {
    common::Close(fd);
    if (auto loop = event_loop_.lock()) {
        loop->RemoveFd(fd);
    }
}


} // namespace upgrade
} // namespace quicx