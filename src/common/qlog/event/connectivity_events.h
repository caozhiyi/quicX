// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_EVENT_CONNECTIVITY_EVENTS
#define COMMON_QLOG_EVENT_CONNECTIVITY_EVENTS

#include <sstream>
#include "common/qlog/event/qlog_event.h"

namespace quicx {
namespace common {

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

}  // namespace common
}  // namespace quicx

#endif
