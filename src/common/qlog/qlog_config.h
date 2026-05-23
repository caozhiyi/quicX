
#ifndef COMMON_QLOG_QLOG_CONFIG
#define COMMON_QLOG_QLOG_CONFIG

#include <cstdint>
#include <string>
#include <vector>

#include <quicx/common/type.h>

namespace quicx {
namespace common {

// Alias types from common/include/type.h
using QlogFileFormat = quicx::QlogFileFormat;
using QlogConfig = quicx::QlogConfig;
using VantagePoint = quicx::VantagePoint;

/**
 * @brief Common fields (recorded once at trace level to avoid per-event
 *        repetition; matches qlog main schema draft-02 §7.1 "common_fields").
 *
 * `protocol_types` is per-schema an array of standardized protocol identifiers
 * (e.g. ["QUIC", "HTTP3"]). The serializer is responsible for emitting the
 * JSON array form.
 */
struct CommonFields {
    std::vector<std::string> protocol_types = {"QUIC", "HTTP3"};
    std::string group_id;

    // Reference time (in milliseconds, UNIX epoch) used when events carry
    // relative timestamps. 0 means "not set / use absolute timestamps".
    uint64_t reference_time_ms = 0;

    // "relative" or "absolute". When "relative", event time is relative to
    // reference_time_ms; when "absolute", event time is wall-clock.
    std::string time_format = "relative";
};

/**
 * @brief qlog configuration info (recorded in Trace)
 */
struct QlogConfiguration {
    uint64_t time_offset = 0;
    // Default to "ms" because the serializer emits time in milliseconds.
    std::string time_units = "ms";
};

}  // namespace common
}  // namespace quicx

#endif
