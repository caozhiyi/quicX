// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_EVENT_TRANSPORT_EVENTS
#define COMMON_QLOG_EVENT_TRANSPORT_EVENTS

#include <sstream>
#include <vector>

#include "common/qlog/event/qlog_event.h"
#include "common/qlog/util/qlog_types.h"

namespace quicx {
namespace common {

/**
 * @brief packet_sent event data
 */
class PacketSentData: public EventData {
public:
    uint64_t packet_number = 0;
    quic::PacketType packet_type = quic::PacketType::kUnknownPacketType;
    std::vector<quic::FrameType> frames;  // Frame types included in this packet
    uint32_t packet_size = 0;

    // Optional fields
    struct RawInfo {
        bool enabled = false;
        std::string payload_hex;  // Hexadecimal string
    } raw;

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"packet_type\":\"" << PacketTypeToQlogString(packet_type) << "\",";
        oss << "\"header\":{";
        oss << "\"packet_number\":" << packet_number << ",";
        oss << "\"packet_size\":" << packet_size;
        oss << "},";

        // Frame list
        oss << "\"frames\":[";
        for (size_t i = 0; i < frames.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"frame_type\":\"" << FrameTypeToQlogString(frames[i]) << "\"}";
        }
        oss << "]";

        // Optional raw data
        if (raw.enabled && !raw.payload_hex.empty()) {
            oss << ",\"raw\":{\"payload\":\"" << raw.payload_hex << "\"}";
        }

        oss << "}";
        return oss.str();
    }
};

/**
 * @brief packet_received event data
 */
class PacketReceivedData: public EventData {
public:
    uint64_t packet_number = 0;
    quic::PacketType packet_type = quic::PacketType::kUnknownPacketType;
    std::vector<quic::FrameType> frames;
    uint32_t packet_size = 0;

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"packet_type\":\"" << PacketTypeToQlogString(packet_type) << "\",";
        oss << "\"header\":{";
        oss << "\"packet_number\":" << packet_number << ",";
        oss << "\"packet_size\":" << packet_size;
        oss << "},";

        // Frame list
        oss << "\"frames\":[";
        for (size_t i = 0; i < frames.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"frame_type\":\"" << FrameTypeToQlogString(frames[i]) << "\"}";
        }
        oss << "]";

        oss << "}";
        return oss.str();
    }
};

/**
 * @brief packets_acked event data
 */
class PacketsAckedData: public EventData {
public:
    struct AckRange {
        uint64_t start;
        uint64_t end;
    };

    std::vector<AckRange> ack_ranges;
    uint32_t ack_delay_us = 0;

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{\"acked_ranges\":[";
        for (size_t i = 0; i < ack_ranges.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "[" << ack_ranges[i].start << "," << ack_ranges[i].end << "]";
        }
        oss << "],\"ack_delay\":" << ack_delay_us << "}";
        return oss.str();
    }
};

/**
 * @brief packet_lost event data
 */
class PacketLostData: public EventData {
public:
    uint64_t packet_number = 0;
    quic::PacketType packet_type = quic::PacketType::kUnknownPacketType;
    std::string trigger = "time_threshold";  // "time_threshold", "packet_threshold", "pto_expired"

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"packet_number\":" << packet_number << ",";
        oss << "\"packet_type\":\"" << PacketTypeToQlogString(packet_type) << "\",";
        oss << "\"trigger\":\"" << trigger << "\"";
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief stream_state_updated event data
 */
class StreamStateUpdatedData: public EventData {
public:
    uint64_t stream_id = 0;
    std::string old_state;
    std::string new_state;

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"stream_id\":" << stream_id << ",";
        oss << "\"old\":\"" << old_state << "\",";
        oss << "\"new\":\"" << new_state << "\"";
        oss << "}";
        return oss.str();
    }
};

}  // namespace common
}  // namespace quicx

#endif
