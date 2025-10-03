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
    : event_loop_(std::move(event_loop)) {

}

UpgradeServer::~UpgradeServer() {
    for (auto fd : listen_fds_) {
        common::Close(fd);
    }
    listen_fds_.clear();
}

bool UpgradeServer::AddListener(UpgradeSettings& settings) {
    // Create appropriate smart handler based on settings
    auto handler = SmartHandlerFactory::CreateHandler(settings, event_loop_);
    auto connection_handler = std::make_shared<ConnectionHandler>(event_loop_, handler);
    
    // Determine which port to use based on HTTPS configuration
    uint16_t port = settings.IsHTTPSEnabled() ? settings.https_port : settings.http_port;
    
    if (port == 0 || port > 65535) {
        common::LOG_ERROR("Invalid listen port: %u", port);
        return false;
    }
    // Create listening socket
    int listen_fd = CreateListenSocket(settings.listen_addr, port);
    if (listen_fd < 0) {
        common::LOG_ERROR("Failed to create listening socket on %s:%d", settings.listen_addr.c_str(), port);
        return false;
    }

    // Add listener to the single event loop
    if (event_loop_->RegisterFd(listen_fd, common::EventType::ET_READ, connection_handler)) {
        common::LOG_INFO("%s listener added on %s:%d", 
                        handler->GetType().c_str(), 
                        settings.listen_addr.c_str(), port);
    } else {
        common::LOG_ERROR("Failed to add %s listener on %s:%d", 
                         handler->GetType().c_str(), 
                         settings.listen_addr.c_str(), port);
        return false;
    }
    listen_fds_.push_back(listen_fd);
    common::LOG_INFO("Listener added successfully");
    return true;
}

int UpgradeServer::CreateListenSocket(const std::string& addr, uint16_t port) {
    // Create socket
    auto result = common::TcpSocket();
    if (result.errno_ != 0) {
        common::LOG_ERROR("Failed to create socket. errno: %d", result.errno_);
        return -1;
    }
    uint64_t listen_fd = result.return_value_;

    // Set non-blocking
    auto ret = common::SocketNoblocking(listen_fd);
    if (ret.errno_ != 0) {
        common::LOG_ERROR("Failed to get socket flags. errno: %d", ret.errno_);
        common::Close(listen_fd);
        return -1;
    }

    // Bind socket
    common::Address address(addr, port);
    ret = common::Bind(listen_fd, address);
    if (ret.errno_ != 0) {
        common::LOG_ERROR("Failed to bind socket. errno: %d", ret.errno_);
        common::Close(listen_fd);
        return -1;
    }

    ret = common::Listen(listen_fd, 1024);
    if (ret.errno_ != 0) {
        common::LOG_ERROR("Failed to listen on socket. errno: %d", ret.errno_);
        common::Close(listen_fd);
        return -1;
    }

    common::LOG_INFO("Listening socket created on %s:%d", addr.c_str(), port);
    return listen_fd;
}

} // namespace upgrade
} // namespace quicx 