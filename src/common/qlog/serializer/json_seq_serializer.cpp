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

    // Per draft-ietf-quic-qlog-main-schema-02 §6.2 ("JSON-SEQ"), the file
    // begins with a single "trace" object that carries top-level metadata
    // (qlog_format / qlog_version / title / description) plus a `trace`
    // sub-object containing vantage_point / common_fields / configuration.
    // Each record is preceded by RS (0x1E) and terminated by LF.
    oss << kJsonSeqRecordSeparator;
    oss << "{";
    oss << "\"qlog_format\":\"" << kQlogFormat << "\",";
    oss << "\"qlog_version\":\"" << kQlogVersion << "\",";
    oss << "\"title\":\"QuicX " << VantagePointToString(vantage_point) << "\",";
    oss << "\"description\":\"QUIC connection trace\",";

    oss << "\"trace\":{";

    // vantage_point: type MUST be one of {client, server, network, unknown};
    // name is descriptive (we use the connection ID when available).
    oss << "\"vantage_point\":{";
    if (!connection_id.empty()) {
        oss << "\"name\":\"" << connection_id << "\",";
    } else {
        oss << "\"name\":\"" << VantagePointToString(vantage_point) << "\",";
    }
    oss << "\"type\":\"" << VantagePointToString(vantage_point) << "\"";
    oss << "},";

    // common_fields
    oss << "\"common_fields\":{";
    bool first = true;

    if (!common_fields.protocol_types.empty()) {
        oss << "\"protocol_types\":[";
        for (size_t i = 0; i < common_fields.protocol_types.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << common_fields.protocol_types[i] << "\"";
        }
        oss << "]";
        first = false;
    }

    if (!common_fields.time_format.empty()) {
        if (!first) oss << ",";
        oss << "\"time_format\":\"" << common_fields.time_format << "\"";
        first = false;
    }

    // draft-02: numeric high-precision time fields MUST be serialized as
    // strings to avoid IEEE-754 precision loss for large epoch timestamps.
    if (common_fields.reference_time_ms != 0) {
        if (!first) oss << ",";
        oss << "\"reference_time\":\"" << common_fields.reference_time_ms << "\"";
        first = false;
    }

    if (!common_fields.group_id.empty()) {
        if (!first) oss << ",";
        oss << "\"group_id\":\"" << common_fields.group_id << "\"";
        first = false;
    }
    oss << "},";

    // configuration
    oss << "\"configuration\":{";
    oss << "\"time_offset\":" << config.time_offset << ",";
    oss << "\"time_units\":\"" << config.time_units << "\"";
    oss << "}";

    oss << "}";  // close trace
    oss << "}\n";

    return oss.str();
}

std::string JsonSeqSerializer::SerializeEvent(const QlogEvent& event) {
    std::ostringstream oss;

    oss << kJsonSeqRecordSeparator;
    oss << "{";

    // draft-02: `time` is serialized as a string-encoded integer in the
    // unit declared by `configuration.time_units` (default ms). Storing
    // it as a string avoids precision loss for absolute epoch timestamps.
    uint64_t time_ms = event.time_us / 1000;
    oss << "\"time\":\"" << time_ms << "\",";

    // Event name
    oss << "\"name\":\"" << event.name << "\",";

    // Event data
    oss << "\"data\":";
    if (event.data) {
        oss << event.data->ToJson();
    } else {
        oss << "{}";
    }

    // Optional fields
    if (!event.group_id.empty()) {
        oss << ",\"group_id\":\"" << event.group_id << "\"";
    }

    oss << "}\n";

    return oss.str();
}

}  // namespace common
}  // namespace quicx
