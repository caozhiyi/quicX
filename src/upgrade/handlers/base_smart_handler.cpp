#include "upgrade/handlers/base_smart_handler.h"
#include "upgrade/core/protocol_detector.h"
#include "upgrade/network/if_tcp_socket.h"
#include "common/log/log.h"

namespace quicx {
namespace upgrade {

BaseSmartHandler::BaseSmartHandler(const UpgradeSettings& settings) 
    : settings_(settings) {
    manager_ = std::make_shared<UpgradeManager>(settings);
}

void BaseSmartHandler::HandleConnect(std::shared_ptr<ITcpSocket> socket, std::shared_ptr<ITcpAction> action) {
    // Initialize connection-specific resources
    if (!InitializeConnection(socket)) {
        common::LOG_ERROR("Failed to initialize %s connection", GetHandlerType().c_str());
        return;
    }
    
    // Create connection context
    ConnectionContext context(socket);
    context_map_[socket] = context;
    
    common::LOG_INFO("New %s connection established", GetHandlerType().c_str());
}

void BaseSmartHandler::HandleRead(std::shared_ptr<ITcpSocket> socket) {
    auto it = context_map_.find(socket);
    if (it == context_map_.end()) {
        common::LOG_ERROR("Socket not found in %s contexts", GetHandlerType().c_str());
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Read data from socket using subclass implementation
    std::vector<uint8_t> data;
    int bytes_read = ReadData(socket, data);
    
    if (bytes_read < 0) {
        common::LOG_ERROR("Failed to read from %s socket: %d", GetHandlerType().c_str(), socket->GetFd());
        HandleClose(socket);
        return;
    }
    
    if (bytes_read == 0) {
        // Connection closed by peer
        common::LOG_INFO("%s connection closed by peer, socket: %d", GetHandlerType().c_str(), socket->GetFd());
        HandleClose(socket);
        return;
    }
    
    // Resize data to actual bytes read
    data.resize(bytes_read);
    
    // Handle data based on connection state
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
        common::LOG_DEBUG("%s upgrade in progress, received %d bytes", GetHandlerType().c_str(), bytes_read);
    } else if (context.state == ConnectionState::UPGRADED) {
        // Protocol upgraded, data should be handled by the upgraded protocol
        // TODO: Forward data to the upgraded protocol handler
        common::LOG_DEBUG("%s protocol upgraded, received %d bytes", GetHandlerType().c_str(), bytes_read);
    } else {
        // Failed state, ignore data
        common::LOG_WARN("%s connection in failed state, ignoring data", GetHandlerType().c_str());
    }
}

void BaseSmartHandler::HandleWrite(std::shared_ptr<ITcpSocket> socket) {
    auto it = context_map_.find(socket);
    if (it == context_map_.end()) {
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Handle write events based on connection state
    if (context.state == ConnectionState::NEGOTIATING) {
        // Send upgrade response if available
        // TODO: Send upgrade response data
        common::LOG_DEBUG("Sending %s upgrade response", GetHandlerType().c_str());
    } else if (context.state == ConnectionState::UPGRADING) {
        // Send upgrade data
        // TODO: Send upgrade data
        common::LOG_DEBUG("Sending %s upgrade data", GetHandlerType().c_str());
    }
}

void BaseSmartHandler::HandleClose(std::shared_ptr<ITcpSocket> socket) {
    auto it = context_map_.find(socket);
    if (it != context_map_.end()) {
        common::LOG_INFO("%s connection closed, socket: %d", GetHandlerType().c_str(), socket->GetFd());
        
        // Clean up connection-specific resources
        CleanupConnection(socket);
        
        context_map_.erase(it);
    }
}

void BaseSmartHandler::HandleProtocolDetection(std::shared_ptr<ITcpSocket> socket, const std::vector<uint8_t>& data) {
    auto it = context_map_.find(socket);
    if (it == context_map_.end()) {
        return;
    }
    
    ConnectionContext& context = it->second;
    context.state = ConnectionState::DETECTING;
    context.initial_data = data;
    
    // Check if we have ALPN negotiated protocol (for HTTPS connections)
    std::string alpn_protocol = GetNegotiatedProtocol(socket);
    if (!alpn_protocol.empty()) {
        common::LOG_INFO("ALPN negotiated protocol: %s", alpn_protocol.c_str());
        
        // Store ALPN protocols for negotiation
        context.alpn_protocols.push_back(alpn_protocol);
        
        // Map ALPN protocol to our Protocol enum for detection
        if (alpn_protocol == "h3") {
            context.detected_protocol = Protocol::HTTP3;
        } else if (alpn_protocol == "h2") {
            context.detected_protocol = Protocol::HTTP2;
        } else if (alpn_protocol == "http/1.1") {
            context.detected_protocol = Protocol::HTTP1_1;
        } else {
            common::LOG_WARN("Unknown ALPN protocol: %s", alpn_protocol.c_str());
        }
        
        if (context.detected_protocol != Protocol::UNKNOWN) {
            OnProtocolDetected(context);
            return;
        }
    }
    
    // Fall back to protocol detection from data
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

void BaseSmartHandler::OnProtocolDetected(ConnectionContext& context) {
    context.state = ConnectionState::NEGOTIATING;
    
    common::LOG_INFO("%s protocol detected: %d", GetHandlerType().c_str(), static_cast<int>(context.detected_protocol));
    
    // Execute upgrade negotiation
    try {
        manager_->ProcessUpgrade(context);
        OnUpgradeComplete(context);
    } catch (const std::exception& e) {
        OnUpgradeFailed(context, e.what());
    }
}

void BaseSmartHandler::OnUpgradeComplete(ConnectionContext& context) {
    context.state = ConnectionState::UPGRADED;
    common::LOG_INFO("%s upgrade completed successfully", GetHandlerType().c_str());
}

void BaseSmartHandler::OnUpgradeFailed(ConnectionContext& context, const std::string& error) {
    context.state = ConnectionState::FAILED;
    common::LOG_ERROR("%s upgrade failed: %s", GetHandlerType().c_str(), error.c_str());
    
    // Send error response to client
    if (context.socket && context.socket->IsValid()) {
        std::string error_response = 
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: " + std::to_string(error.length()) + "\r\n"
            "\r\n" + error;
        
        WriteData(context.socket, error_response);
    }
    
    // Clean up connection
    context_map_.erase(context.socket);
}

} // namespace upgrade
} // namespace quicx 