#include <thread>

#include "common/log/log.h"
#include "common/log/stdout_logger.h"
#include "upgrade/network/tcp_action.h"
#include "upgrade/server/upgrade_server.h"

namespace quicx {
namespace upgrade {

UpgradeServer::UpgradeServer() {
    // Constructor
}

bool UpgradeServer::Init(LogLevel level) {
    log_level_ = level;
    
    // Initialize logging system
    std::shared_ptr<common::Logger> log = std::make_shared<common::StdoutLogger>();
    common::LOG_SET(log);
    common::LOG_SET_LEVEL(common::LogLevel(level));
    
    common::LOG_INFO("Upgrade server initialized");
    return true;
}

bool UpgradeServer::AddListener(UpgradeSettings& settings) {
    if (!running_) {
        running_ = true;
    }
    
    // Start listener with appropriate handler
    StartListener(settings);
    
    common::LOG_INFO("Listeners added successfully");
    return true;
}

void UpgradeServer::StartListener(const UpgradeSettings& settings) {
    // Create appropriate smart handler based on settings
    auto handler = SmartHandlerFactory::CreateHandler(settings);
    handlers_.push_back(handler);
    
    // Determine which port to use based on HTTPS configuration
    uint16_t port = settings.IsHTTPSEnabled() ? settings.https_port : settings.http_port;
    
    auto tcp_action = std::make_shared<TcpAction>();
    
    if (tcp_action->Init(settings.listen_addr, port, handler)) {
        listeners_.push_back(tcp_action);
        common::LOG_INFO("%s listener started on %s:%d", 
                        settings.IsHTTPSEnabled() ? "HTTPS" : "HTTP", 
                        settings.listen_addr, port);
    } else {
        common::LOG_ERROR("Failed to start %s listener on %s:%d", 
                         settings.IsHTTPSEnabled() ? "HTTPS" : "HTTP", 
                         settings.listen_addr, port);
    }
}

void UpgradeServer::Stop() {
    running_ = false;
    
    // Stop all listeners
    for (auto& listener : listeners_) {
        listener->Stop();
    }
    
    common::LOG_INFO("Upgrade server stopped");
}

void UpgradeServer::Join() {
    // Wait for all listener threads to finish
    for (auto& listener : listeners_) {
        listener->Join();
    }
    
    common::LOG_INFO("Upgrade server joined");
}

} // namespace upgrade
} // namespace quicx 