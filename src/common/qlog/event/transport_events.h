// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_EVENT_TRANSPORT_EVENTS
#define COMMON_QLOG_EVENT_TRANSPORT_EVENTS

#include <memory>
#include <sstream>
#include <vector>

#include "common/qlog/event/qlog_event.h"
#include "common/qlog/util/qlog_types.h"
#include "common/qlog/util/quic_frames.h"
#include "quic/frame/if_frame.h"

namespace quicx {
namespace common {

namespace detail {

/**
 * @brief Emit the qlog "header" object for a packet event.
 *
 * Per draft-ietf-quic-qlog-quic-events §4.4 (PacketHeader):
 *   - packet_type, packet_number are mandatory.
 *   - For long-header packets, scid/dcid/version are present.
 *   - For Initial, token is also present.
 *   - flags / length are optional.
 */
inline void WritePacketHeader(std::ostringstream& oss,
                              quic::PacketType packet_type,
                              uint64_t packet_number,
                              const std::string& scid,
                              const std::string& dcid,
                              uint32_t version,
                              const std::string& token) {
    oss << "\"header\":{";
    oss << "\"packet_type\":\"" << PacketTypeToQlogString(packet_type) << "\",";
    oss << "\"packet_number\":" << packet_number;
    if (!scid.empty()) {
        oss << ",\"scid\":\"" << scid << "\"";
    }
    if (!dcid.empty()) {
        oss << ",\"dcid\":\"" << dcid << "\"";
    }
    if (version != 0 && packet_type != quic::PacketType::k1RttPacketType
        && packet_type != quic::PacketType::kUnknownPacketType) {
        // Print version as 0xXXXXXXXX hex string per common qlog convention.
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08x", version);
        oss << ",\"version\":\"" << buf << "\"";
    }
    if (!token.empty()) {
        oss << ",\"token\":{\"data\":\"" << token << "\"}";
    }
    oss << "}";
}

/**
 * @brief Emit the qlog "frames" array.
 *
 * Prefer the rich `frame_objects` form if any are provided; fall back to the
 * `frame_types` enum list (frame_type only) for tests / call sites that have
 * not yet been migrated.
 */
inline void WriteFrames(std::ostringstream& oss,
                        const std::vector<std::shared_ptr<quic::IFrame>>& frame_objects,
                        const std::vector<quic::FrameType>& frame_types) {
    oss << "\"frames\":[";
    if (!frame_objects.empty()) {
        for (size_t i = 0; i < frame_objects.size(); ++i) {
            if (i > 0) oss << ",";
            oss << FrameToJson(frame_objects[i]);
        }
    } else {
        for (size_t i = 0; i < frame_types.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "{\"frame_type\":\"" << FrameTypeToQlogString(frame_types[i]) << "\"}";
        }
    }
    oss << "]";
}

}  // namespace detail

/**
 * @brief packet_sent event data
 */
class PacketSentData: public EventData {
public:
    uint64_t packet_number = 0;
    quic::PacketType packet_type = quic::PacketType::kUnknownPacketType;

    // Preferred: full frame objects, will be serialized with all qlog fields.
    std::vector<std::shared_ptr<quic::IFrame>> frame_objects;
    // Fallback / backward-compat: frame type enum list, only "frame_type" emitted.
    std::vector<quic::FrameType> frames;

    uint32_t packet_size = 0;

    // Long-header metadata (optional; empty/zero means "not applicable")
    std::string scid;     // hex string
    std::string dcid;     // hex string
    uint32_t version = 0;
    std::string token;    // hex string (Initial packets only)

    // Optional fields
    struct RawInfo {
        bool enabled = false;
        std::string payload_hex;  // Hexadecimal string
    } raw;

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        detail::WritePacketHeader(oss, packet_type, packet_number, scid, dcid, version, token);

        // RawInfo: raw.length is the on-the-wire size.
        oss << ",\"raw\":{\"length\":" << packet_size;
        if (raw.enabled && !raw.payload_hex.empty()) {
            oss << ",\"payload\":\"" << raw.payload_hex << "\"";
        }
        oss << "},";

        detail::WriteFrames(oss, frame_objects, frames);
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

    std::vector<std::shared_ptr<quic::IFrame>> frame_objects;
    std::vector<quic::FrameType> frames;

    uint32_t packet_size = 0;

    std::string scid;
    std::string dcid;
    uint32_t version = 0;
    std::string token;

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        detail::WritePacketHeader(oss, packet_type, packet_number, scid, dcid, version, token);

        oss << ",\"raw\":{\"length\":" << packet_size << "},";

        detail::WriteFrames(oss, frame_objects, frames);
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief packets_acked event data
 *
 * NOTE: per draft-ietf-quic-qlog-quic-events, the field name is
 *       `packet_numbers` (a flat list). We keep `acked_ranges` for
 *       backward compatibility with existing tests but ALSO emit
 *       `packet_numbers` for spec compliance.
 */
class PacketsAckedData: public EventData {
public:
    struct AckRange {
        uint64_t start;
        uint64_t end;
    };

    std::vector<AckRange> ack_ranges;
    uint32_t ack_delay_us = 0;
    // "Initial" / "Handshake" / "ApplicationData" — optional but recommended.
    std::string packet_number_space;

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        if (!packet_number_space.empty()) {
            oss << "\"packet_number_space\":\"" << packet_number_space << "\",";
        }
        // Spec-correct: explicit packet number list.
        oss << "\"packet_numbers\":[";
        bool first_pn = true;
        for (const auto& r : ack_ranges) {
            for (uint64_t pn = r.start; pn <= r.end; ++pn) {
                if (!first_pn) oss << ",";
                oss << pn;
                first_pn = false;
            }
        }
        oss << "],";
        // Backward-compat: keep acked_ranges as well.
        oss << "\"acked_ranges\":[";
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
        oss << "\"header\":{";
        oss << "\"packet_type\":\"" << PacketTypeToQlogString(packet_type) << "\",";
        oss << "\"packet_number\":" << packet_number;
        oss << "},";
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

/**
 * @brief packet_dropped event data
 */
class PacketDroppedData: public EventData {
public:
    quic::PacketType packet_type = quic::PacketType::kUnknownPacketType;
    uint32_t packet_size = 0;
    std::string trigger;  // "header_decrypt_error", "payload_decrypt_error", "key_unavailable",
                          // "unexpected_packet_type", "initial_too_small", "unsupported_version",
                          // "draining_state", "closing_state_decrypt_failure"

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"header\":{";
        oss << "\"packet_type\":\"" << PacketTypeToQlogString(packet_type) << "\"";
        oss << "},";
        oss << "\"raw\":{\"length\":" << packet_size << "},";
        oss << "\"trigger\":\"" << trigger << "\"";
        oss << "}";
        return oss.str();
    }
};

/**
 * @brief packet_buffered event data
 */
class PacketBufferedData: public EventData {
public:
    quic::PacketType packet_type = quic::PacketType::kUnknownPacketType;
    uint32_t packet_size = 0;
    std::string trigger;  // "out_of_order", "coalesced"

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"header\":{";
        oss << "\"packet_type\":\"" << PacketTypeToQlogString(packet_type) << "\"";
        oss << "},";
        oss << "\"raw\":{\"length\":" << packet_size << "},";
        oss << "\"trigger\":\"" << trigger << "\"";
        oss << "}";
        return oss.str();
    }
};

}  // namespace common
}  // namespace quicx

#endif
