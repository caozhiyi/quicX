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
    
    // Create smart handler
    handler_ = std::make_shared<SmartHandler>(settings);
    
    // Start HTTP/1.1 listener
    if (settings.enable_http1) {
        StartHTTP1Listener(settings);
    }
    
    // Start HTTPS listener
    if (settings.enable_http2 || settings.enable_http3) {
        StartHTTPSListener(settings);
    }
    
    common::LOG_INFO("Listeners added successfully");
    return true;
}

void UpgradeServer::StartHTTP1Listener(const UpgradeSettings& settings) {
    auto tcp_action = std::make_shared<TcpAction>();
    
    if (tcp_action->Init(settings.listen_addr, settings.http_port, handler_)) {
        listeners_.push_back(tcp_action);
        common::LOG_INFO("HTTP/1.1 listener started on {}:{}", settings.listen_addr, settings.http_port);
    } else {
        common::LOG_ERROR("Failed to start HTTP/1.1 listener on {}:{}", settings.listen_addr, settings.http_port);
    }
}

void UpgradeServer::StartHTTPSListener(const UpgradeSettings& settings) {
    auto tcp_action = std::make_shared<TcpAction>();
    
    if (tcp_action->Init(settings.listen_addr, settings.https_port, handler_)) {
        listeners_.push_back(tcp_action);
        common::LOG_INFO("HTTPS listener started on {}:{}", settings.listen_addr, settings.https_port);
    } else {
        common::LOG_ERROR("Failed to start HTTPS listener on {}:{}", settings.listen_addr, settings.https_port);
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