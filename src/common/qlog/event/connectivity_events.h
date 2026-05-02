// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_EVENT_CONNECTIVITY_EVENTS
#define COMMON_QLOG_EVENT_CONNECTIVITY_EVENTS

#include <sstream>
#include "common/qlog/event/qlog_event.h"

namespace quicx {
namespace common {

/**
 * @brief server_listening event data
 *
 * Logged when a QUIC server starts listening on a local address
 */
class ServerListeningData: public EventData {
public:
    std::string ip;
    uint16_t port = 0;
    std::string ip_version = "ipv4";  // "ipv4" or "ipv6"

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"ip_version\":\"" << ip_version << "\",";
        oss << "\"ip\":\"" << ip << "\",";
        oss << "\"port\":" << port;
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief connection_started event data
 */
class ConnectionStartedData: public EventData {
public:
    std::string src_ip;
    uint16_t src_port = 0;
    std::string dst_ip;
    uint16_t dst_port = 0;

    std::string src_cid;  // hexadecimal string
    std::string dst_cid;

    std::string protocol = "QUIC";
    std::string ip_version = "ipv4";  // "ipv4" or "ipv6"

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"ip_version\":\"" << ip_version << "\",";
        oss << "\"src_ip\":\"" << src_ip << "\",";
        oss << "\"dst_ip\":\"" << dst_ip << "\",";
        oss << "\"protocol\":\"" << protocol << "\",";
        oss << "\"src_port\":" << src_port << ",";
        oss << "\"dst_port\":" << dst_port << ",";
        oss << "\"src_cid\":\"" << src_cid << "\",";
        oss << "\"dst_cid\":\"" << dst_cid << "\"";
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief connection_closed event data
 */
class ConnectionClosedData: public EventData {
public:
    uint64_t error_code = 0;
    std::string reason;
    std::string trigger = "clean";  // "clean", "application", "error", "stateless_reset"

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"error_code\":" << error_code << ",";
        oss << "\"reason\":\"" << EscapeJson(reason) << "\",";
        oss << "\"trigger\":\"" << trigger << "\"";
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief connection_state_updated event data
 */
class ConnectionStateUpdatedData: public EventData {
public:
    std::string old_state;  // "initial", "handshake", "connected", "closing", "draining", "closed"
    std::string new_state;

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"old\":\"" << old_state << "\",";
        oss << "\"new\":\"" << new_state << "\"";
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief connection_id_updated event data
 */
class ConnectionIdUpdatedData: public EventData {
public:
    std::string owner;      // "local" or "remote"
    std::string old_id;     // Old connection ID (hex string, may be empty)
    std::string new_id;     // New connection ID (hex string, may be empty for retire)
    std::string trigger;    // "new_connection_id", "retire_connection_id", "retire_prior_to",
                            // "cid_rotation", "pool_replenish"

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"owner\":\"" << owner << "\",";
        if (!old_id.empty()) {
            oss << "\"old\":\"" << old_id << "\",";
        }
        if (!new_id.empty()) {
            oss << "\"new\":\"" << new_id << "\",";
        }
        oss << "\"trigger\":\"" << trigger << "\"";
        oss << "}";
        return oss.str();
    }
};

}  // namespace common
}  // namespace quicx

#endif
