#include "common/log/log.h"
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/core/protocol_detector.h"
#include "upgrade/handlers/base_smart_handler.h"

namespace quicx {
namespace upgrade {

BaseSmartHandler::BaseSmartHandler(const UpgradeSettings& settings):
    settings_(settings) {
    manager_ = std::make_shared<UpgradeManager>(settings);
}

void BaseSmartHandler::HandleConnect(std::shared_ptr<ITcpSocket> socket, std::shared_ptr<ITcpAction> action) {
    // Store TCP action weak pointer
    tcp_action_ = action;
    // Ensure dependencies are initialized
    if (action) {
        action->Init();
    }
    if (auto ev = event_driver_.lock()) {
        ev->Init();
    }
    
    // Initialize connection-specific resources
    if (!InitializeConnection(socket)) {
        common::LOG_ERROR("Failed to initialize %s connection", GetType().c_str());
        return;
    }
    
    // Create and store connection context directly in the map (avoid default construction)
    auto insert_result = connections_.emplace(socket, ConnectionContext(socket));
    ConnectionContext& context = insert_result.first->second;
    
    // Add negotiation timeout timer (30 seconds)
    if (auto tcp_action = tcp_action_.lock()) {
        context.negotiation_timer_id = tcp_action->AddTimer(
            [this, socket]() {
                HandleNegotiationTimeout(socket);
            },
            30000  // 30 seconds timeout
        );
        
        if (context.negotiation_timer_id > 0) {
            common::LOG_DEBUG("Negotiation timeout timer added for socket %d", socket->GetFd());
        } else {
            common::LOG_ERROR("Failed to add negotiation timeout timer for socket %d", socket->GetFd());
        }
    }
    
    common::LOG_INFO("New %s connection established", GetType().c_str());
}

void BaseSmartHandler::HandleRead(std::shared_ptr<ITcpSocket> socket) {
    auto it = connections_.find(socket);
    if (it == connections_.end()) {
        common::LOG_ERROR("Socket not found in %s contexts", GetType().c_str());
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Read data from socket using subclass implementation
    std::vector<uint8_t> data;
    int bytes_read = ReadData(socket, data);
    
    if (bytes_read < 0) {
        common::LOG_ERROR("Failed to read from %s socket: %d", GetType().c_str(), socket->GetFd());
        HandleClose(socket);
        return;
    }
    
    if (bytes_read == 0) {
        // Connection closed by peer
        common::LOG_INFO("%s connection closed by peer, socket: %d", GetType().c_str(), (int)socket->GetFd());
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
    } else if (context.state == ConnectionState::UPGRADED) {
        // Protocol upgraded, data should be handled by the upgraded protocol
        common::LOG_DEBUG("%s protocol upgraded, received %d bytes", GetType().c_str(), bytes_read);
    } else {
        // Failed state, ignore data
        common::LOG_WARN("%s connection in failed state, ignoring data", GetType().c_str());
    }
}

void BaseSmartHandler::HandleWrite(std::shared_ptr<ITcpSocket> socket) {
    auto it = connections_.find(socket);
    if (it == connections_.end()) {
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Handle write events based on connection state
    if (context.state == ConnectionState::NEGOTIATING) {
        common::LOG_DEBUG("Continuing to send %s upgrade response", GetType().c_str());
        TrySendResponse(context);
        return;
    }

    // Let subclass handle writes even if there is no pending response
    // This also helps tests verify write path is invoked
    WriteData(socket, std::string());
}

void BaseSmartHandler::HandleClose(std::shared_ptr<ITcpSocket> socket) {
    auto it = connections_.find(socket);
    if (it != connections_.end()) {
        ConnectionContext& context = it->second;
        
        // Remove negotiation timeout timer if still active
        if (context.negotiation_timer_id > 0) {
            if (auto tcp_action = tcp_action_.lock()) {
                tcp_action->RemoveTimer(context.negotiation_timer_id);
                context.negotiation_timer_id = 0;
                common::LOG_DEBUG("Negotiation timeout timer removed for socket %d", socket->GetFd());
            }
        }
        
        common::LOG_INFO("%s connection closed, socket: %d", GetType().c_str(), socket->GetFd());
        
        // Clean up connection-specific resources
        CleanupConnection(socket);
        
        connections_.erase(it);
    }
}

void BaseSmartHandler::HandleProtocolDetection(std::shared_ptr<ITcpSocket> socket, const std::vector<uint8_t>& data) {
    auto it = connections_.find(socket);
    if (it == connections_.end()) {
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
    
    common::LOG_INFO("%s protocol detected: %d", GetType().c_str(), static_cast<int>(context.detected_protocol));
    
    // Execute upgrade negotiation
    manager_->ProcessUpgrade(context);
    OnUpgradeComplete(context);
}

void BaseSmartHandler::OnUpgradeComplete(ConnectionContext& context) {
    context.state = ConnectionState::UPGRADED;
    
    // Remove negotiation timeout timer
    if (context.negotiation_timer_id > 0) {
        if (auto tcp_action = tcp_action_.lock()) {
            tcp_action->RemoveTimer(context.negotiation_timer_id);
            context.negotiation_timer_id = 0;
            common::LOG_DEBUG("Negotiation timeout timer removed for socket %d", context.socket->GetFd());
        }
    }
    
    common::LOG_INFO("%s upgrade completed successfully", GetType().c_str());
}

void BaseSmartHandler::OnUpgradeFailed(ConnectionContext& context, const std::string& error) {
    context.state = ConnectionState::FAILED;
    
    // Remove negotiation timeout timer
    if (context.negotiation_timer_id > 0) {
        if (auto tcp_action = tcp_action_.lock()) {
            tcp_action->RemoveTimer(context.negotiation_timer_id);
            context.negotiation_timer_id = 0;
            common::LOG_DEBUG("Negotiation timeout timer removed for socket %d", context.socket->GetFd());
        }
    }
    
    common::LOG_ERROR("%s upgrade failed: %s", GetType().c_str(), error.c_str());

    manager_->HandleUpgradeFailure(context, error);
    TrySendResponse(context);
}

void BaseSmartHandler::TrySendResponse(ConnectionContext& context) {
    if (context.pending_response.empty() || context.response_sent >= context.pending_response.size()) {
        return; // No pending response or already sent completely
    }
    
    // Send remaining data
    std::vector<uint8_t> data_to_send(
        context.pending_response.begin() + context.response_sent,
        context.pending_response.end()
    );
    // Route through subclass write path
    std::string payload(data_to_send.begin(), data_to_send.end());
    int bytes_sent = WriteData(context.socket, payload);
    
    if (bytes_sent > 0) {
        context.response_sent += bytes_sent;
        
        if (context.response_sent >= context.pending_response.size()) {
            // Response sent completely
            common::LOG_INFO("Response sent completely (%zu bytes)", context.pending_response.size());
            context.pending_response.clear();
            context.response_sent = 0;
            
            // Remove WRITE event since we're done
            if (auto event_driver = event_driver_.lock()) {
                event_driver->ModifyFd(context.socket->GetFd(), EventType::READ);
            }
        } else {
            // Partial send, register WRITE event to continue
            common::LOG_DEBUG("Partial response sent (%d/%zu bytes), registering WRITE event", bytes_sent, context.pending_response.size());
            if (auto event_driver = event_driver_.lock()) {
                event_driver->ModifyFd(context.socket->GetFd(), static_cast<EventType>(static_cast<int>(EventType::READ) | static_cast<int>(EventType::WRITE)));
            }
        }
    } else if (bytes_sent < 0) {
        // Send error
        common::LOG_ERROR("Failed to send response");
        context.pending_response.clear();
        context.response_sent = 0;
        
        // Remove WRITE event on error
        if (auto event_driver = event_driver_.lock()) {
            event_driver->ModifyFd(context.socket->GetFd(), EventType::READ);
        }
    } else {
        // bytes_sent == 0 means would block, register WRITE event to retry
        common::LOG_DEBUG("Send would block, registering WRITE event");
        if (auto event_driver = event_driver_.lock()) {
            event_driver->ModifyFd(context.socket->GetFd(), static_cast<EventType>(static_cast<int>(EventType::READ) | static_cast<int>(EventType::WRITE)));
        }
    }
}

void BaseSmartHandler::HandleNegotiationTimeout(std::shared_ptr<ITcpSocket> socket) {
    auto it = connections_.find(socket);
    if (it == connections_.end()) {
        common::LOG_DEBUG("Connection already closed for socket %d", socket->GetFd());
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Check if negotiation is still in progress
    if (context.state == ConnectionState::INITIAL || 
        context.state == ConnectionState::DETECTING || 
        context.state == ConnectionState::NEGOTIATING) {
        
        common::LOG_WARN("Negotiation timeout for %s connection, socket: %d", GetType().c_str(), socket->GetFd());
        
        // Close the connection due to timeout
        socket->Close();
        
        // Clean up connection context
        CleanupConnection(socket);
        connections_.erase(it);
    } else {
        // Negotiation completed or failed, timer is no longer needed
        common::LOG_DEBUG("Negotiation timeout timer fired but negotiation already completed for socket %d", socket->GetFd());
    }
}

} // namespace upgrade
} // namespace quicx 