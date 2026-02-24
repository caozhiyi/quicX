
#ifndef COMMON_QLOG_QLOG_CONFIG
#define COMMON_QLOG_QLOG_CONFIG

#include "common/include/type.h"

namespace quicx {
namespace common {

// Alias types from common/include/type.h
using QlogFileFormat = quicx::QlogFileFormat;
using QlogConfig = quicx::QlogConfig;
using VantagePoint = quicx::VantagePoint;

/**
 * @brief Common fields (reduce repetition)
 */
struct CommonFields {
    std::string protocol_type = "QUIC";
    std::string group_id;
};

/**
 * @brief qlog configuration info (recorded in Trace)
 */
struct QlogConfiguration {
    uint64_t time_offset = 0;
    std::string time_units = "us";
};

}  // namespace common
}  // namespace quicx

#endif
