#include "upgrade/handlers/smart_handler.h"
#include "upgrade/core/protocol_detector.h"
#include "upgrade/network/if_tcp_socket.h"
#include "common/log/log.h"

namespace quicx {
namespace upgrade {

SmartHandler::SmartHandler(const UpgradeSettings& settings) 
    : settings_(settings) {
    manager_ = std::make_shared<UpgradeManager>(settings);
}

void SmartHandler::HandleConnect(std::shared_ptr<ITcpSocket> socket, std::shared_ptr<ITcpAction> action) {
    // Create connection context
    ConnectionContext context(socket);
    context_map_[socket] = context;
    
    common::LOG_INFO("New connection established");
}

void SmartHandler::HandleRead(std::shared_ptr<ITcpSocket> socket) {
    auto it = context_map_.find(socket);
    if (it == context_map_.end()) {
        common::LOG_ERROR("Socket not found in contexts");
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Read data from socket
    std::vector<uint8_t> data;
    int bytes_read = socket->Recv(data, 4096); // Read up to 4KB
    
    if (bytes_read < 0) {
        common::LOG_ERROR("Failed to read from socket: {}", socket->GetFd());
        HandleClose(socket);
        return;
    }
    
    if (bytes_read == 0) {
        // Connection closed by peer
        common::LOG_INFO("Connection closed by peer, socket: {}", socket->GetFd());
        HandleClose(socket);
        return;
    }
    
    // Resize data to actual bytes read
    data.resize(bytes_read);
    
    if (context.state == ConnectionState::INITIAL) {
        // First read, perform protocol detection
        HandleProtocolDetection(socket, data);
    } else if (context.state == ConnectionState::DETECTING) {
        // Still detecting protocol, append data and retry
        context.initial_data.insert(context.initial_data.end(), data.begin(), data.end());
        HandleProtocolDetection(socket, context.initial_data);
    } else if (context.state == ConnectionState::NEGOTIATING) {
        // Protocol negotiation in progress, store additional data
        context.initial_data.insert(context.initial_data.end(), data.begin(), data.end());
    } else if (context.state == ConnectionState::UPGRADING) {
        // Protocol upgrade in progress, forward data to upgrade manager
        // TODO: Forward data to upgrade manager for processing
        common::LOG_DEBUG("Upgrade in progress, received {} bytes", bytes_read);
    } else if (context.state == ConnectionState::UPGRADED) {
        // Protocol upgraded, data should be handled by the upgraded protocol
        // TODO: Forward data to the upgraded protocol handler
        common::LOG_DEBUG("Protocol upgraded, received {} bytes", bytes_read);
    } else {
        // Failed state, ignore data
        common::LOG_WARN("Connection in failed state, ignoring data");
    }
}

void SmartHandler::HandleWrite(std::shared_ptr<ITcpSocket> socket) {
    auto it = context_map_.find(socket);
    if (it == context_map_.end()) {
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Handle write events based on connection state
    if (context.state == ConnectionState::NEGOTIATING) {
        // Send upgrade response if available
        // TODO: Send upgrade response data
        common::LOG_DEBUG("Sending upgrade response");
    } else if (context.state == ConnectionState::UPGRADING) {
        // Send upgrade data
        // TODO: Send upgrade data
        common::LOG_DEBUG("Sending upgrade data");
    }
}

void SmartHandler::HandleClose(std::shared_ptr<ITcpSocket> socket) {
    auto it = context_map_.find(socket);
    if (it != context_map_.end()) {
        common::LOG_INFO("Connection closed, socket: {}", socket->GetFd());
        context_map_.erase(it);
    }
}

void SmartHandler::HandleProtocolDetection(std::shared_ptr<ITcpSocket> socket, const std::vector<uint8_t>& data) {
    auto it = context_map_.find(socket);
    if (it == context_map_.end()) {
        return;
    }
    
    ConnectionContext& context = it->second;
    context.state = ConnectionState::DETECTING;
    context.initial_data = data;
    
    // Detect protocol
    Protocol detected = ProtocolDetector::Detect(data);
    context.detected_protocol = detected;
    
    if (detected != Protocol::UNKNOWN) {
        OnProtocolDetected(context);
    } else {
        // If we have enough data but still can't detect, it might be an unsupported protocol
        if (data.size() >= 1024) { // Wait for at least 1KB of data
            OnUpgradeFailed(context, "Unsupported or unknown protocol");
        }
        // Otherwise, keep waiting for more data in the next HandleRead call
    }
}

void SmartHandler::OnProtocolDetected(ConnectionContext& context) {
    context.state = ConnectionState::NEGOTIATING;
    
    common::LOG_INFO("Protocol detected: {}", static_cast<int>(context.detected_protocol));
    
    // Execute upgrade negotiation
    try {
        manager_->ProcessUpgrade(context);
        OnUpgradeComplete(context);
    } catch (const std::exception& e) {
        OnUpgradeFailed(context, e.what());
    }
}

void SmartHandler::OnUpgradeComplete(ConnectionContext& context) {
    context.state = ConnectionState::UPGRADED;
    common::LOG_INFO("Upgrade completed successfully");
}

void SmartHandler::OnUpgradeFailed(ConnectionContext& context, const std::string& error) {
    context.state = ConnectionState::FAILED;
    common::LOG_ERROR("Upgrade failed: {}", error);
    
    // Send error response to client
    if (context.socket && context.socket->IsValid()) {
        std::string error_response = 
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " + std::to_string(error.length()) + "\r\n"
            "\r\n" + error;
        
        context.socket->Send(error_response);
    }
    
    // Clean up connection
    context_map_.erase(context.socket);
}

} // namespace upgrade
} // namespace quicx 