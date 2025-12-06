// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

#ifndef COMMON_QLOG_EVENT_RECOVERY_EVENTS
#define COMMON_QLOG_EVENT_RECOVERY_EVENTS

#include <cstdint>
#include <sstream>

#include "common/qlog/event/qlog_event.h"

namespace quicx {
namespace common {

/**
 * @brief recovery_metrics_updated event data
 */
class RecoveryMetricsData: public EventData {
public:
    uint32_t min_rtt_us = 0;
    uint32_t smoothed_rtt_us = 0;
    uint32_t latest_rtt_us = 0;
    uint32_t rtt_variance_us = 0;

    uint64_t cwnd_bytes = 0;
    uint64_t bytes_in_flight = 0;
    uint64_t ssthresh = UINT64_MAX;

    uint64_t pacing_rate_bps = 0;  // bits per second

    std::string ToJson() const override {
        std::ostringstream oss;
        oss << "{";
        oss << "\"min_rtt\":" << min_rtt_us << ",";
        oss << "\"smoothed_rtt\":" << smoothed_rtt_us << ",";
        oss << "\"latest_rtt\":" << latest_rtt_us << ",";
        oss << "\"rtt_variance\":" << rtt_variance_us << ",";
        oss << "\"cwnd\":" << cwnd_bytes << ",";
        oss << "\"bytes_in_flight\":" << bytes_in_flight;

        if (ssthresh != UINT64_MAX) {
            oss << ",\"ssthresh\":" << ssthresh;
        }
        if (pacing_rate_bps > 0) {
            oss << ",\"pacing_rate\":" << pacing_rate_bps;
        }

        oss << "}";
        return oss.str();
    }
};

/**
 * @brief congestion_state_updated event data
 */
class CongestionStateUpdatedData: public EventData {
public:
    std::string old_state;  // "slow_start", "congestion_avoidance", "recovery", "application_limited"
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
