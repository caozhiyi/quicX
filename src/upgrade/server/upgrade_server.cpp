#include <memory>
#include "common/log/log.h"
#include "common/network/io_handle.h"
#include "upgrade/server/upgrade_server.h"
#include "common/network/if_event_driver.h"
#include "upgrade/server/connection_handler.h"
#include "upgrade/handlers/smart_handler_factory.h"

namespace quicx {
namespace upgrade {

std::unique_ptr<IUpgrade> IUpgrade::MakeUpgrade(std::shared_ptr<common::IEventLoop> event_loop) {
    return std::make_unique<UpgradeServer>(event_loop);
}

UpgradeServer::UpgradeServer(std::shared_ptr<common::IEventLoop> event_loop)
    : event_loop_(event_loop) {

}

UpgradeServer::~UpgradeServer() {
    // Best-effort: deregister every listening fd from the event loop before
    // closing it, so the loop drops the matching weak_ptr<IFdHandler> entry
    // and never tries to dispatch to an already-destroyed ConnectionHandler
    // during the racing teardown of the loop itself.
    auto loop = event_loop_.lock();
    for (auto& entry : listeners_) {
        if (loop) {
            loop->RemoveFd(entry.fd);
        }
        common::Close(entry.fd);
    }
    listeners_.clear();
}

bool UpgradeServer::AddListener(UpgradeSettings& settings) {
    auto loop = event_loop_.lock();
    if (!loop) return false;

    // The upgrade module is meant to live next to a real H3/QUIC server and
    // advertise it via Alt-Svc on TCP. To do that we need BOTH a plaintext
    // socket (so a browser's first http://host:port/ request gets a 200 +
    // Alt-Svc) and a TLS socket (so https://host:port/ also works and can
    // negotiate h2/http1.1 via ALPN). The previous implementation picked
    // ONE port based on whether credentials were configured, which meant
    // that as soon as you handed the server a cert+key, port 8080 was
    // silently never bound -- chrome/curl on http:// got "connection
    // refused" and h3 was undiscoverable.
    //
    // New behaviour: bind whichever of {http_port, https_port} the caller
    // actually populated, with the appropriate handler for each. Each
    // listener owns its own ConnectionHandler / ISmartHandler pair so the
    // plaintext path can never accidentally feed bytes into an SSL state
    // machine and vice versa.
    const bool has_file_pair = !settings.cert_file.empty() && !settings.key_file.empty();
    const bool has_pem_pair = (settings.cert_pem != nullptr) && (settings.key_pem != nullptr);
    const bool https_enabled = has_file_pair || has_pem_pair;

    bool started_any = false;

    auto bind_one = [&](uint16_t port, SmartHandlerFactory::HandlerKind kind, const char* label) -> bool {
        if (port == 0) {
            return true;  // not requested, skip silently
        }
        if (port > 65535) {
            LOG_ERROR("Invalid %s listen port: %u", label, port);
            return false;
        }
        auto handler = SmartHandlerFactory::CreateHandler(settings, loop, kind);
        if (!handler) {
            LOG_ERROR("Failed to create %s handler", label);
            return false;
        }
        auto connection_handler = std::make_shared<ConnectionHandler>(loop, handler);

        int listen_fd = CreateListenSocket(settings.listen_addr, port);
        if (listen_fd < 0) {
            LOG_ERROR("Failed to create %s listening socket on %s:%d",
                      label, settings.listen_addr.c_str(), port);
            return false;
        }

        if (loop->RegisterFd(listen_fd, common::EventType::ET_READ, connection_handler)) {
            LOG_INFO("%s listener added on %s:%d",
                     handler->GetType().c_str(),
                     settings.listen_addr.c_str(), port);
        } else {
            LOG_ERROR("Failed to add %s listener on %s:%d",
                      handler->GetType().c_str(),
                      settings.listen_addr.c_str(), port);
            common::Close(listen_fd);
            return false;
        }
        // CRITICAL: keep a strong reference to the ConnectionHandler that
        // services this fd. EventLoop::fd_to_handler_ stores it as a
        // std::weak_ptr, so without this push_back the shared_ptr falls
        // off the end of bind_one() and the very next epoll/kqueue wakeup
        // for `listen_fd` produces "No handler found for fd N" and the
        // accept() loop never runs. The smart handler that connection_handler
        // points at is transitively kept alive through ConnectionHandler::
        // handler_ — that path is also what keeps client_fd's registration
        // valid (client fds register `handler_` directly on the loop).
        listeners_.push_back(ListenEntry{static_cast<uint32_t>(listen_fd), connection_handler});
        started_any = true;
        return true;
    };

    // Plaintext H1/H2 listener (used to serve Alt-Svc to browsers that hit
    // http:// first).
    if (!bind_one(settings.http_port, SmartHandlerFactory::HandlerKind::kHttp, "HTTP")) {
        return false;
    }

    // TLS listener (only meaningful if a cert/key is configured).
    if (https_enabled) {
        if (!bind_one(settings.https_port, SmartHandlerFactory::HandlerKind::kHttps, "HTTPS")) {
            return false;
        }
    } else if (settings.https_port != 0) {
        LOG_WARN("https_port=%u was set but no cert/key configured; skipping TLS listener",
                 settings.https_port);
    }

    if (!started_any) {
        LOG_ERROR("AddListener: neither http_port nor https_port were usable");
        return false;
    }

    LOG_INFO("Listener added successfully");
    return true;
}

int UpgradeServer::CreateListenSocket(const std::string& addr, uint16_t port) {
    // Create socket
    auto result = common::TcpSocket();
    if (result.error_code_ != 0) {
        LOG_ERROR("Failed to create socket. errno: %d", result.error_code_);
        return -1;
    }
    uint64_t listen_fd = result.return_value_;

    // Set non-blocking
    auto ret = common::SocketNoblocking(listen_fd);
    if (ret.error_code_ != 0) {
        LOG_ERROR("Failed to get socket flags. errno: %d", ret.error_code_);
        common::Close(listen_fd);
        return -1;
    }

    // Bind socket
    common::Address address(addr, port);
    ret = common::Bind(listen_fd, address);
    if (ret.error_code_ != 0) {
        LOG_ERROR("Failed to bind socket. errno: %d", ret.error_code_);
        common::Close(listen_fd);
        return -1;
    }

    ret = common::Listen(listen_fd, 1024);
    if (ret.error_code_ != 0) {
        LOG_ERROR("Failed to listen on socket. errno: %d", ret.error_code_);
        common::Close(listen_fd);
        return -1;
    }

    LOG_INFO("Listening socket created on %s:%d", addr.c_str(), port);
    return listen_fd;
}

} // namespace upgrade
} // namespace quicx 