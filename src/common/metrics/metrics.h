#ifndef COMMON_METRICS_METRICS_H
#define COMMON_METRICS_METRICS_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "common/include/type.h"

namespace quicx {
namespace common {

using MetricID = uint32_t;
constexpr MetricID kInvalidMetricID = static_cast<MetricID>(-1);

class Metrics {
public:
    // --- 1. Initialization ---
    static bool Initialize(const MetricsConfig& config);

    // --- 2. Dynamic Registration (for custom metrics) ---
    static MetricID RegisterCounter(
        const std::string& name, const std::string& help = "", const std::map<std::string, std::string>& labels = {});

    static MetricID RegisterGauge(
        const std::string& name, const std::string& help = "", const std::map<std::string, std::string>& labels = {});

    static MetricID RegisterHistogram(const std::string& name, const std::string& help = "",
        const std::vector<uint64_t>& buckets = {}, const std::map<std::string, std::string>& labels = {});

    // --- 3. Operations (Hot Path) ---
    static void CounterInc(MetricID id, uint64_t val = 1);

    static void GaugeInc(MetricID id, int64_t val = 1);
    static void GaugeDec(MetricID id, int64_t val = 1);
    static void GaugeSet(MetricID id, int64_t val);

    static void HistogramObserve(MetricID id, uint64_t val);

    // --- 4. Export ---
    static std::string ExportPrometheus();
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_METRICS_METRICS_H
