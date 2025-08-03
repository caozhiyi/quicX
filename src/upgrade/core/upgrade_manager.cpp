#include "upgrade/core/upgrade_manager.h"
#include "upgrade/core/version_negotiator.h"
#include "common/log/log.h"
#include <sstream>

namespace quicx {
namespace upgrade {

UpgradeManager::UpgradeManager(const UpgradeSettings& settings) 
    : settings_(settings) {
    // Upgrade manager only handles protocol negotiation
    // HTTP/3 implementation should be handled externally
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
    last_result_ = VersionNegotiator::Negotiate(context, settings_);
    
    if (!last_result_.success) {
        HandleUpgradeFailure(context, last_result_.error_message);
        return;
    }
    
    // Send appropriate response based on negotiation result
    SendUpgradeResponse(context, last_result_);
}

void UpgradeManager::SendUpgradeResponse(ConnectionContext& context, const NegotiationResult& result) {
    if (result.target_protocol == Protocol::HTTP3) {
        // Send upgrade response indicating HTTP/3 is preferred
        if (result.upgrade_data.empty()) {
            // No upgrade data means client doesn't support HTTP/3
            SendFailureResponse(context, "HTTP/3 not supported by client");
            return;
        }
        
        // Send the upgrade response
        context.socket->Send(std::string(result.upgrade_data.begin(), result.upgrade_data.end()));
        common::LOG_INFO("Sent upgrade response for HTTP/3");
    } else {
        // For other protocols, just log the result
        common::LOG_INFO("Protocol negotiation completed: %d", static_cast<int>(result.target_protocol));
    }
}

void UpgradeManager::SendFailureResponse(ConnectionContext& context, const std::string& error) {
    std::string error_response = 
        "HTTP/1.1 400 Bad Request\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(error.length()) + "\r\n"
        "\r\n" + error;
    
    context.socket->Send(error_response);
    common::LOG_ERROR("Upgrade failed: %s", error.c_str());
}

void UpgradeManager::HandleUpgradeFailure(ConnectionContext& context, const std::string& error) {
    SendFailureResponse(context, error);
    context.socket->Close();
    
    // Clean up connection context
    connections_.erase(context.socket);
}

} // namespace upgrade
} // namespace quicx 