#ifndef UPGRADE_CORE_CONNECTION_STATE_H
#define UPGRADE_CORE_CONNECTION_STATE_H

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include "upgrade/network/if_tcp_action.h"
#include "upgrade/network/if_tcp_socket.h"

namespace quicx {
namespace upgrade {

// Connection state enumeration
enum class ConnectionState {
    INITIAL,           // Initial state
    DETECTING,         // Protocol detection in progress
    NEGOTIATING,       // Protocol negotiation in progress
    UPGRADING,         // Protocol upgrade in progress
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
    std::unordered_map<std::string, std::string> headers;
    std::vector<std::string> alpn_protocols;
    std::chrono::steady_clock::time_point created_time;
    
    // Constructor initializes the connection context
    ConnectionContext(std::shared_ptr<ITcpSocket> sock) 
        : socket(sock), created_time(std::chrono::steady_clock::now()) {}
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_CORE_CONNECTION_STATE_H 