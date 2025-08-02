#include "upgrade/core/upgrade_manager.h"
#include "upgrade/core/version_negotiator.h"
#include "common/log/log.h"
#include <sstream>

namespace quicx {
namespace upgrade {

UpgradeManager::UpgradeManager(const UpgradeSettings& settings) 
    : settings_(settings) {
    // Initialize QUIC and HTTP/3 servers
    // TODO: Actual implementation needs to create these server instances
}

void UpgradeManager::HandleConnection(std::shared_ptr<ITcpSocket> socket) {
    // Create connection context
    ConnectionContext context(socket);
    connections_[socket] = context;
    
    // Read initial data
    std::vector<uint8_t> initial_data;
    // TODO: Read initial data from socket
    context.initial_data = initial_data;
    
    // Process upgrade
    ProcessUpgrade(context);
}

void UpgradeManager::ProcessUpgrade(ConnectionContext& context) {
    // Perform version negotiation
    auto result = VersionNegotiator::Negotiate(context, settings_);
    
    if (!result.success) {
        HandleUpgradeFailure(context, result.error_message);
        return;
    }
    
    // Execute corresponding upgrade logic based on negotiation result
    switch (result.target_protocol) {
        case Protocol::HTTP3:
            HandleHTTP3Upgrade(context);
            break;
        case Protocol::HTTP2:
            HandleHTTP2Upgrade(context);
            break;
        case Protocol::HTTP1_1:
            // Keep HTTP/1.1, no upgrade needed
            break;
        default:
            HandleUpgradeFailure(context, "Unsupported protocol");
            break;
    }
}

void UpgradeManager::HandleHTTP1Upgrade(ConnectionContext& context) {
    // Send HTTP/1.1 upgrade response
    std::string response = 
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: h3\r\n"
        "Connection: Upgrade\r\n"
        "\r\n";
    
    context.socket->Send(response);
    
    // Start HTTP/3 connection
    StartHTTP3Connection(context);
}

void UpgradeManager::HandleHTTP2Upgrade(ConnectionContext& context) {
    // Send HTTP/2 GOAWAY frame
    SendHTTP2GoAway(context);
    
    // Start HTTP/3 connection
    StartHTTP3Connection(context);
}

void UpgradeManager::HandleHTTP3Upgrade(ConnectionContext& context) {
    // Start HTTP/3 connection directly
    StartHTTP3Connection(context);
}

void UpgradeManager::StartHTTP3Connection(ConnectionContext& context) {
    // TODO: Implement HTTP/3 connection startup logic
    // 1. Create QUIC connection
    // 2. Establish HTTP/3 stream
    // 3. Handle HTTP/3 requests
    
    common::LOG_INFO("Starting HTTP/3 connection for socket");
}

void UpgradeManager::SendHTTP2GoAway(ConnectionContext& context) {
    // TODO: Implement HTTP/2 GOAWAY frame sending
    // This needs to generate correct HTTP/2 frame format
    
    common::LOG_INFO("Sending HTTP/2 GOAWAY frame");
}

void UpgradeManager::HandleUpgradeFailure(ConnectionContext& context, const std::string& error) {
    common::LOG_ERROR("Upgrade failed: {}", error);
    
    // Send error response
    std::string error_response = 
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(error.length()) + "\r\n"
        "\r\n" + error;
    
    context.socket->Send(error_response);
    context.socket->Close();
    
    // Clean up connection context
    connections_.erase(context.socket);
}

} // namespace upgrade
} // namespace quicx 