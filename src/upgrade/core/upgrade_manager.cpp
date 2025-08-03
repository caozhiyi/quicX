#include "upgrade/core/upgrade_manager.h"
#include "upgrade/core/version_negotiator.h"
#include "common/log/log.h"
#include <sstream>

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
        if (result.upgrade_data.empty()) {
            // No upgrade data means client doesn't support HTTP/3
            SendFailureResponse(context, "HTTP/3 not supported by client");
            return;
        }
        
        // Store the upgrade response for potential partial sends
        context.pending_response = result.upgrade_data;
        context.response_sent = 0;
        
        // Try to send the response
        TrySendResponse(context);
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
    
    // Try to send the response
    TrySendResponse(context);
    common::LOG_ERROR("Upgrade failed: %s", error.c_str());
}

void UpgradeManager::TrySendResponse(ConnectionContext& context) {
    if (context.pending_response.empty() || context.response_sent >= context.pending_response.size()) {
        return; // No pending response or already sent completely
    }
    
    // Send remaining data
    size_t remaining = context.pending_response.size() - context.response_sent;
    std::vector<uint8_t> data_to_send(
        context.pending_response.begin() + context.response_sent,
        context.pending_response.end()
    );
    
    int bytes_sent = context.socket->Send(data_to_send);
    
    if (bytes_sent > 0) {
        context.response_sent += bytes_sent;
        
        if (context.response_sent >= context.pending_response.size()) {
            // Response sent completely
            common::LOG_INFO("Response sent completely (%zu bytes)", context.pending_response.size());
            context.pending_response.clear();
            context.response_sent = 0;
        } else {
            // Partial send, will continue in HandleWrite
            common::LOG_DEBUG("Partial response sent (%d/%zu bytes)", bytes_sent, context.pending_response.size());
        }
    } else if (bytes_sent < 0) {
        // Send error
        common::LOG_ERROR("Failed to send response");
        context.pending_response.clear();
        context.response_sent = 0;
    }
    // bytes_sent == 0 means would block, will retry in HandleWrite
}

void UpgradeManager::HandleUpgradeFailure(ConnectionContext& context, const std::string& error) {
    SendFailureResponse(context, error);
    context.socket->Close();
    
    // Clean up connection context
    connections_.erase(context.socket);
}

ConnectionContext* UpgradeManager::GetConnectionContext(std::shared_ptr<ITcpSocket> socket) {
    auto it = connections_.find(socket);
    if (it != connections_.end()) {
        return &it->second;
    }
    return nullptr;
}

void UpgradeManager::AddConnectionContext(std::shared_ptr<ITcpSocket> socket, const ConnectionContext& context) {
    connections_[socket] = context;
}

void UpgradeManager::RemoveConnectionContext(std::shared_ptr<ITcpSocket> socket) {
    connections_.erase(socket);
}

void UpgradeManager::ContinueSendResponse(std::shared_ptr<ITcpSocket> socket) {
    ConnectionContext* context = GetConnectionContext(socket);
    if (context) {
        TrySendResponse(*context);
    }
}

} // namespace upgrade
} // namespace quicx 