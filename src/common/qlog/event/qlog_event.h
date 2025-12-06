// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_EVENT_QLOG_EVENT
#define COMMON_QLOG_EVENT_QLOG_EVENT

#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

namespace quicx {
namespace common {

/**
 * @brief Base class for event data (abstract)
 */
class EventData {
public:
    virtual ~EventData() = default;

    /**
     * @brief Serialize to JSON string
     */
    virtual std::string ToJson() const = 0;

protected:
    /**
     * @brief JSON string escaping
     */
    static std::string EscapeJson(const std::string& str) {
        std::ostringstream oss;
        for (char c : str) {
            switch (c) {
                case '"':
                    oss << "\\\"";
                    break;
                case '\\':
                    oss << "\\\\";
                    break;
                case '\n':
                    oss << "\\n";
                    break;
                case '\r':
                    oss << "\\r";
                    break;
                case '\t':
                    oss << "\\t";
                    break;
                case '\b':
                    oss << "\\b";
                    break;
                case '\f':
                    oss << "\\f";
                    break;
                default:
                    if (c < 32) {
                        // Control characters
                        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    } else {
                        oss << c;
                    }
            }
        }
        return oss.str();
    }
};

/**
 * @brief qlog event structure
 */
struct QlogEvent {
    uint64_t time_us;                 // Microsecond timestamp (relative or absolute)
    std::string name;                 // Event name (e.g. "quic:packet_sent")
    std::unique_ptr<EventData> data;  // Event data (polymorphic)

    // Optional fields
    std::string group_id;       // Event grouping
    uint8_t protocol_type = 0;  // Protocol type (QUIC=0, HTTP3=1)

    QlogEvent():
        time_us(0),
        protocol_type(0) {}

    // Move constructor and assignment
    QlogEvent(QlogEvent&& other) noexcept:
        time_us(other.time_us),
        name(std::move(other.name)),
        data(std::move(other.data)),
        group_id(std::move(other.group_id)),
        protocol_type(other.protocol_type) {}

    QlogEvent& operator=(QlogEvent&& other) noexcept {
        if (this != &other) {
            time_us = other.time_us;
            name = std::move(other.name);
            data = std::move(other.data);
            group_id = std::move(other.group_id);
            protocol_type = other.protocol_type;
        }
        return *this;
    }

    // Disable copy
    QlogEvent(const QlogEvent&) = delete;
    QlogEvent& operator=(const QlogEvent&) = delete;
};

}  // namespace common
}  // namespace quicx

#endif
