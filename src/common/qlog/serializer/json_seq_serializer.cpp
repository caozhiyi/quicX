// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#include "common/qlog/serializer/json_seq_serializer.h"
#include "common/qlog/util/qlog_constants.h"
#include "common/qlog/util/qlog_types.h"

namespace quicx {
namespace common {

std::string JsonSeqSerializer::SerializeTraceHeader(const std::string& connection_id, VantagePoint vantage_point,
    const CommonFields& common_fields, const QlogConfiguration& config) {
    std::ostringstream oss;

    // First line: File header
    oss << "{\"qlog_format\":\"" << kQlogFormat << "\",\"qlog_version\":\"" << kQlogVersion << "\"}\n";

    // Second line: Trace metadata
    oss << "{";
    oss << "\"title\":\"QuicX " << VantagePointToString(vantage_point) << "\",";
    oss << "\"description\":\"QUIC connection trace\",";

    // Vantage point
    oss << "\"vantage_point\":{";
    oss << "\"name\":\"" << VantagePointToString(vantage_point) << "\",";
    oss << "\"type\":\"" << VantagePointToString(vantage_point) << "\"";
    oss << "},";

    // Common fields
    oss << "\"common_fields\":{";
    oss << "\"protocol_type\":\"" << common_fields.protocol_type << "\"";
    if (!common_fields.group_id.empty()) {
        oss << ",\"group_id\":\"" << common_fields.group_id << "\"";
    }
    oss << "},";

    // Configuration
    oss << "\"configuration\":{";
    oss << "\"time_offset\":" << config.time_offset << ",";
    oss << "\"time_units\":\"" << config.time_units << "\"";
    oss << "},";

    // Empty events array (placeholder)
    oss << "\"events\":[]";
    oss << "}\n";

    return oss.str();
}

std::string JsonSeqSerializer::SerializeEvent(const QlogEvent& event) {
    std::ostringstream oss;

    oss << "{";

    // Timestamp (convert to milliseconds, keep decimals)
    oss << "\"time\":" << std::fixed << std::setprecision(3) << (event.time_us / 1000.0) << ",";

    // Event name
    oss << "\"name\":\"" << event.name << "\",";

    // Event data
    oss << "\"data\":";
    if (event.data) {
        oss << event.data->ToJson();
    } else {
        oss << "{}";
    }

    // 可选字段
    if (!event.group_id.empty()) {
        oss << ",\"group_id\":\"" << event.group_id << "\"";
    }

    oss << "}\n";

    return oss.str();
}

}  // namespace common
}  // namespace quicx
