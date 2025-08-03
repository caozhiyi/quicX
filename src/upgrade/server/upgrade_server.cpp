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
    // Initialize TCP action if not already done
    if (!tcp_action_) {
        tcp_action_ = std::make_shared<TcpAction>();
        if (!tcp_action_->Init()) {
            common::LOG_ERROR("Failed to initialize TCP action");
            return false;
        }
        running_ = true;
    }
    
    // Create appropriate smart handler based on settings
    auto handler = SmartHandlerFactory::CreateHandler(settings);
    handlers_.push_back(handler);
    
    // Determine which port to use based on HTTPS configuration
    uint16_t port = settings.IsHTTPSEnabled() ? settings.https_port : settings.http_port;
    
    // Add listener to the single TCP action
    if (tcp_action_->AddListener(settings.listen_addr, port, handler)) {
        common::LOG_INFO("%s listener added on %s:%d", 
                        handler->GetType().c_str(), 
                        settings.listen_addr, port);
    } else {
        common::LOG_ERROR("Failed to add %s listener on %s:%d", 
                         handler->GetType().c_str(), 
                         settings.listen_addr, port);
        return false;
    }
    
    common::LOG_INFO("Listener added successfully");
    return true;
}

void UpgradeServer::Stop() {
    running_ = false;
    
    // Stop the single TCP action
    if (tcp_action_) {
        tcp_action_->Stop();
    }
    
    common::LOG_INFO("Upgrade server stopped");
}

void UpgradeServer::Join() {
    // Wait for the TCP action thread to finish
    if (tcp_action_) {
        tcp_action_->Join();
    }
    
    common::LOG_INFO("Upgrade server joined");
}

} // namespace upgrade
} // namespace quicx 