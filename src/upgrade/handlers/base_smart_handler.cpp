#include "common/log/log.h"
#include "upgrade/network/tcp_socket.h"
#include "upgrade/core/protocol_detector.h"
#include "common/network/if_event_driver.h"
#include "upgrade/handlers/base_smart_handler.h"

namespace quicx {
namespace upgrade {

BaseSmartHandler::BaseSmartHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop):
    settings_(settings), event_loop_(event_loop) {
    manager_ = std::make_shared<UpgradeManager>(settings);
}

void BaseSmartHandler::OnConnect(uint32_t fd) {
    auto socket = std::make_shared<TcpSocket>(fd);
    // Initialize connection-specific resources
    if (!InitializeConnection(socket)) {
        common::LOG_ERROR("Failed to initialize %s connection", GetType().c_str());
        return;
    }
    
    // Create and store connection context directly in the map (avoid default construction)
    auto insert_result = connections_.emplace(fd, ConnectionContext(socket));
    ConnectionContext& context = insert_result.first->second;
    
    // Add negotiation timeout timer (30 seconds)
    if (auto event_loop = event_loop_.lock()) {
        context.negotiation_timer_id = event_loop->AddTimer(
            [this, fd]() {
                HandleNegotiationTimeout(fd);
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

void BaseSmartHandler::OnRead(uint32_t fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        common::LOG_ERROR("Socket not found in %s contexts", GetType().c_str());
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Read data from socket using subclass implementation
    std::vector<uint8_t> data;
    int bytes_read = ReadData(context.socket, data);
    
    common::LOG_DEBUG("Read %d bytes from %s socket: %d, read data: %s", bytes_read, GetType().c_str(), context.socket->GetFd(), data.data());

    if (bytes_read < 0) {
        common::LOG_ERROR("Failed to read from %s socket: %d", GetType().c_str(), context.socket->GetFd());
        OnClose(fd);
        return;
    }
    
    if (bytes_read == 0) {
        // Connection closed by peer
        common::LOG_INFO("%s connection closed by peer, socket: %d", GetType().c_str(), context.socket->GetFd());
        OnClose(fd);
        return;
    }

    // Resize data to actual bytes read
    data.resize(bytes_read);
    
    // Handle data based on connection state
    if (context.state == ConnectionState::INITIAL) {
        // First read, perform protocol detection
        HandleProtocolDetection(fd, data);
    } else if (context.state == ConnectionState::DETECTING) {
        // Still detecting protocol, append data and retry
        context.initial_data.insert(context.initial_data.end(), data.begin(), data.end());
        HandleProtocolDetection(fd, context.initial_data);
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

void BaseSmartHandler::OnWrite(uint32_t fd) {
    auto it = connections_.find(fd);
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
}

void BaseSmartHandler::OnError(uint32_t fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }

    ConnectionContext& context = it->second;
    common::LOG_ERROR("%s connection error on socket: %d", GetType().c_str(), context.socket->GetFd());
    OnClose(fd);
}

void BaseSmartHandler::OnClose(uint32_t fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
        
    ConnectionContext& context = it->second;
        
    // Remove negotiation timeout timer if still active
    if (context.negotiation_timer_id > 0) {
        if (auto event_loop = event_loop_.lock()) {
            event_loop->RemoveTimer(context.negotiation_timer_id);
            context.negotiation_timer_id = 0;
            common::LOG_DEBUG("Negotiation timeout timer removed for socket %d", context.socket->GetFd());
        }
    }
        
    common::LOG_INFO("%s connection closed, socket: %d", GetType().c_str(), context.socket->GetFd());
        
    // Clean up connection-specific resources
    CleanupConnection(context.socket);
        
    connections_.erase(it);
}

void BaseSmartHandler::HandleProtocolDetection(uint32_t fd, const std::vector<uint8_t>& data) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        return;
    }
    
    ConnectionContext& context = it->second;
    context.state = ConnectionState::DETECTING;
    context.initial_data = data;
    
    // Check if we have ALPN negotiated protocol (for HTTPS connections)
    std::string alpn_protocol = GetNegotiatedProtocol(context.socket);
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
    TrySendResponse(context);
}

void BaseSmartHandler::OnUpgradeComplete(ConnectionContext& context) {
    context.state = ConnectionState::UPGRADED;
    
    // Remove negotiation timeout timer
    if (context.negotiation_timer_id > 0) {
        if (auto event_loop = event_loop_.lock()) {
            event_loop->RemoveTimer(context.negotiation_timer_id);
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
        if (auto event_loop = event_loop_.lock()) {
            event_loop->RemoveTimer(context.negotiation_timer_id);
            context.negotiation_timer_id = 0;
            common::LOG_DEBUG("Negotiation timeout timer removed for socket %d", context.socket->GetFd());
        }
    }
    
    common::LOG_ERROR("%s upgrade failed: %s", GetType().c_str(), error.c_str());

    manager_->HandleUpgradeFailure(context, error);
    TrySendResponse(context);
}

void BaseSmartHandler::TrySendResponse(ConnectionContext& context) {
    if (context.state == ConnectionState::UPGRADED) {
        return;
    }

    if (context.pending_response.empty() || context.response_sent >= context.pending_response.size()) {
        return; // No pending response or already sent completely
    }
    
    // Send remaining data
    std::vector<uint8_t> data_to_send(
        context.pending_response.begin() + context.response_sent,
        context.pending_response.end()
    );
    // Route through subclass write path
    int bytes_sent = WriteData(context.socket, data_to_send);

    if (bytes_sent >= data_to_send.size()) {
        common::LOG_INFO("Response sent completely (%zu bytes)", context.pending_response.size());
        
        context.pending_response.clear();
        context.response_sent = 0;
        
        // Only call OnUpgradeComplete if not in FAILED state
        if (context.state != ConnectionState::FAILED) {
            OnUpgradeComplete(context);
        }
        return;
    }
    
    if (bytes_sent > 0) {
        context.response_sent += bytes_sent;
        
        if (context.response_sent >= context.pending_response.size()) {
            // Response sent completely
            common::LOG_INFO("Response sent completely (%zu bytes)", context.pending_response.size());
            context.pending_response.clear();
            context.response_sent = 0;
            
            // Only call OnUpgradeComplete if not in FAILED state
            if (context.state != ConnectionState::FAILED) {
                OnUpgradeComplete(context);
            }
            
            // Remove WRITE event since we're done
            if (auto event_driver = event_loop_.lock()) {
                event_driver->ModifyFd(context.socket->GetFd(), common::EventType::ET_READ);
            }
        } else {
            // Partial send, register WRITE event to continue
            common::LOG_DEBUG("Partial response sent (%d/%zu bytes), registering WRITE event", bytes_sent, context.pending_response.size());
            if (auto event_driver = event_loop_.lock()) {
                event_driver->ModifyFd(context.socket->GetFd(), static_cast<common::EventType>(static_cast<int>(common::EventType::ET_READ) | static_cast<int>(common::EventType::ET_WRITE)));
            }
        }

    } else if (bytes_sent < 0) {
        // Send error
        common::LOG_ERROR("Failed to send response");
        context.pending_response.clear();
        context.response_sent = 0;
        
        // Remove WRITE event on error
        if (auto event_driver = event_loop_.lock()) {
            event_driver->ModifyFd(context.socket->GetFd(), common::EventType::ET_READ);
        }

    } else {
        // bytes_sent == 0 means would block, register WRITE event to retry
        common::LOG_DEBUG("Send would block, registering WRITE event");
        if (auto event_driver = event_loop_.lock()) {
            event_driver->ModifyFd(context.socket->GetFd(), static_cast<int32_t>(common::EventType::ET_READ) | static_cast<int32_t>(common::EventType::ET_WRITE));
        }
    }
}

void BaseSmartHandler::HandleNegotiationTimeout(uint32_t fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
        common::LOG_DEBUG("Connection already closed for socket %d", fd);
        return;
    }
    
    ConnectionContext& context = it->second;
    
    // Check if negotiation is still in progress
    if (context.state == ConnectionState::INITIAL || 
        context.state == ConnectionState::DETECTING || 
        context.state == ConnectionState::NEGOTIATING) {
        
        common::LOG_WARN("Negotiation timeout for %s connection, socket: %d", GetType().c_str(), context.socket->GetFd());
        
        // Close the connection due to timeout
        context.socket->Close();
        
        // Clean up connection context
        CleanupConnection(context.  socket);
        connections_.erase(it);

    } else {
        // Negotiation completed or failed, timer is no longer needed
        common::LOG_DEBUG("Negotiation timeout timer fired but negotiation already completed for socket %d", context.socket->GetFd());
    }
}

} // namespace upgrade
} // namespace quicx 