#ifndef UPGRADE_HANDLERS_CONNECTION_CONTEXT_H
#define UPGRADE_HANDLERS_CONNECTION_CONTEXT_H

#include <memory>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include "upgrade/network/if_tcp_socket.h"

namespace quicx {
namespace upgrade {

// Connection state enumeration
enum class ConnectionState {
    INITIAL,           // Initial state
    DETECTING,         // Protocol detection in progress
    NEGOTIATING,       // Protocol negotiation in progress
    UPGRADED,          // Successfully upgraded
    FAILED             // Upgrade failed
};

// Protocol enumeration
enum class Protocol {
    UNKNOWN,
    HTTP1_1,
    HTTP2,
    HTTP3
};

// Connection context containing all connection-related information
struct ConnectionContext {
    std::shared_ptr<ITcpSocket> socket;
    ConnectionState state = ConnectionState::INITIAL;
    Protocol detected_protocol = Protocol::UNKNOWN;
    Protocol target_protocol = Protocol::UNKNOWN;
    std::vector<uint8_t> initial_data;
    std::vector<std::string> alpn_protocols;
    std::chrono::steady_clock::time_point created_time;
    
    // Pending response data for partial sends
    std::vector<uint8_t> pending_response;
    size_t response_sent = 0;  // Bytes already sent
    
    // Constructor initializes the connection context
    ConnectionContext(std::shared_ptr<ITcpSocket> sock) 
        : socket(sock), created_time(std::chrono::steady_clock::now()) {}
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_CONNECTION_CONTEXT_H 