#include "common/log/log.h"
#include "upgrade/core/upgrade_manager.h"
#include "upgrade/core/version_negotiator.h"

namespace quicx {
namespace upgrade {

UpgradeManager::UpgradeManager(const UpgradeSettings& settings) 
    : settings_(settings) {
    // Upgrade manager only handles protocol negotiation
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
        if (!result.upgrade_data.empty()) {
            // Store the upgrade response for potential partial sends
            context.pending_response = result.upgrade_data;
            context.response_sent = 0;
            common::LOG_INFO("Upgrade response prepared for HTTP/3");
        } else {
            // Direct HTTP/3, nothing to send
            common::LOG_INFO("Direct HTTP/3 detected, no upgrade response needed");
        }
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
    
    // Store error response for potential partial sends
    context.pending_response = std::vector<uint8_t>(error_response.begin(), error_response.end());
    context.response_sent = 0;
    
    common::LOG_ERROR("Upgrade failed: %s", error.c_str());
}

void UpgradeManager::HandleUpgradeFailure(ConnectionContext& context, const std::string& error) {
    SendFailureResponse(context, error);
    if (context.socket) {
        context.socket->Close();
    }
}

} // namespace upgrade
} // namespace quicx 