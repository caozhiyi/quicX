// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_EVENT_SECURITY_EVENTS
#define COMMON_QLOG_EVENT_SECURITY_EVENTS

#include <sstream>
#include "common/qlog/event/qlog_event.h"

namespace quicx {
namespace common {

/**
 * @brief security:key_updated event data
 *
 * Logged when a new encryption key is installed (initial, handshake, 1-RTT, or key update)
 */
class KeyUpdatedData: public EventData {
public:
    std::string key_type;            // "initial", "handshake", "1rtt", "0rtt"
    std::string trigger = "tls";     // "tls", "key_update", "retry"
    bool is_write = false;           // true=write/send key, false=read/recv key
    uint32_t generation = 0;         // Key generation (incremented on key update)

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"key_type\":\"" << key_type << "\",";
        oss << "\"trigger\":\"" << trigger << "\",";
        oss << "\"generation\":" << generation << ",";
        oss << "\"is_write\":" << (is_write ? "true" : "false");
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief security:key_discarded event data
 *
 * Logged when encryption keys are discarded (e.g., Initial/Handshake keys after handshake)
 */
class KeyDiscardedData: public EventData {
public:
    std::string key_type;            // "initial", "handshake", "0rtt"
    std::string trigger = "tls";     // "tls", "handshake_done"

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"key_type\":\"" << key_type << "\",";
        oss << "\"trigger\":\"" << trigger << "\"";
        oss << "}";
        return oss.str();
    }
};

}  // namespace common
}  // namespace quicx

#endif
