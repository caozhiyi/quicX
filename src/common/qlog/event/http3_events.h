// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_EVENT_HTTP3_EVENTS
#define COMMON_QLOG_EVENT_HTTP3_EVENTS

#include <sstream>
#include "common/qlog/event/qlog_event.h"

namespace quicx {
namespace common {

/**
 * @brief Convert HTTP/3 frame type to qlog string
 */
inline const char* Http3FrameTypeToString(uint16_t frame_type) {
    switch (frame_type) {
        case 0x00: return "data";
        case 0x01: return "headers";
        case 0x03: return "cancel_push";
        case 0x04: return "settings";
        case 0x05: return "push_promise";
        case 0x07: return "goaway";
        case 0x0d: return "max_push_id";
        default:   return "unknown";
    }
}

/**
 * @brief http3:frame_created event data
 *
 * Logged when an HTTP/3 frame is encoded and sent
 */
class Http3FrameCreatedData: public EventData {
public:
    uint16_t frame_type = 0;
    uint64_t stream_id = 0;
    uint64_t length = 0;     // Frame payload length

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"frame_type\":\"" << Http3FrameTypeToString(frame_type) << "\",";
        oss << "\"stream_id\":" << stream_id << ",";
        oss << "\"length\":" << length;
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief http3:frame_parsed event data
 *
 * Logged when an HTTP/3 frame is received and decoded
 */
class Http3FrameParsedData: public EventData {
public:
    uint16_t frame_type = 0;
    uint64_t stream_id = 0;
    uint64_t length = 0;     // Frame payload length

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"frame_type\":\"" << Http3FrameTypeToString(frame_type) << "\",";
        oss << "\"stream_id\":" << stream_id << ",";
        oss << "\"length\":" << length;
        oss << "}";
        return oss.str();
    }
};

}  // namespace common
}  // namespace quicx

#endif
