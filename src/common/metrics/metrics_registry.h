#ifndef COMMON_METRICS_METRICS_REGISTRY
#define COMMON_METRICS_METRICS_REGISTRY

#include <mutex>
#include <vector>
#include <unordered_map>

#include "common/metrics/metrics_internal.h"

namespace quicx {
namespace common {

class ThreadMetricStorage;

// Global Registry - manages metric metadata and thread registration
class GlobalRegistry {
public:
    static GlobalRegistry* Instance();

    MetricID Register(const std::string& name, const std::string& help,
        const std::map<std::string, std::string>& labels, MetricType type, const std::vector<uint64_t>& buckets = {});

    const MetricMeta* GetMeta(MetricID id);

    void RegisterThread(ThreadMetricStorage* storage);
    void UnregisterThread(ThreadMetricStorage* storage);

    void Collect(std::vector<uint64_t>& counters, std::vector<int64_t>& gauges,
        std::vector<std::unique_ptr<HistogramStorage>>& histograms);

private:
    std::mutex mutex_;
    MetricID next_id_ = 0;
    std::vector<MetricMeta> metas_;
    std::unordered_map<MetricKey, MetricID, MetricKeyHash> key_to_id_;

    std::vector<ThreadMetricStorage*> active_threads_;

    // Dead threads accumulation
    std::vector<uint64_t> dead_counters_;
    std::vector<int64_t> dead_gauges_;

    struct DeadHistogram {
        std::vector<uint64_t> bucket_counts;
        uint64_t sum = 0;
        uint64_t count = 0;
    };
    std::vector<DeadHistogram> dead_histograms_;
};

// Thread Local Storage - manages per-thread metric arrays
class ThreadMetricStorage {
public:
    ThreadMetricStorage();
    ~ThreadMetricStorage();

    std::atomic<uint64_t>& GetCounter(MetricID id);
    std::atomic<int64_t>& GetGauge(MetricID id);
    HistogramStorage& GetHistogram(MetricID id, const std::vector<uint64_t>& buckets);

    friend class GlobalRegistry;

private:
    void GrowCounters(size_t size);
    void GrowGauges(size_t size);
    void GrowHistograms(size_t size);

    std::vector<std::unique_ptr<std::atomic<uint64_t>>> counters_;
    std::vector<std::unique_ptr<std::atomic<int64_t>>> gauges_;
    std::vector<HistogramStorage> histograms_;
};

// Get thread-local storage instance
ThreadMetricStorage& GetThreadStorage();

}  // namespace common
}  // namespace quicx

#endif  // COMMON_METRICS_METRICS_REGISTRY_H
