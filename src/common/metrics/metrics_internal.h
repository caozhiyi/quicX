#ifndef COMMON_METRICS_METRICS_INTERNAL
#define COMMON_METRICS_METRICS_INTERNAL

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/metrics/metrics.h"

namespace quicx {
namespace common {

// Internal metric type enum
enum class MetricType { kCounter, kGauge, kHistogram };

// Metric key for deduplication
struct MetricKey {
    std::string name;
    std::map<std::string, std::string> labels;

    bool operator==(const MetricKey& other) const { return name == other.name && labels == other.labels; }
};

// Hash function for MetricKey
struct MetricKeyHash {
    std::size_t operator()(const MetricKey& k) const {
        std::size_t h = std::hash<std::string>{}(k.name);
        for (const auto& pair : k.labels) {
            h ^= std::hash<std::string>{}(pair.first) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<std::string>{}(pair.second) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

// Metric metadata
struct MetricMeta {
    MetricID id;
    MetricType type;
    std::string name;
    std::string help;
    std::map<std::string, std::string> labels;
    std::vector<uint64_t> buckets;  // For Histogram
};

// Histogram storage
struct HistogramStorage {
    std::unique_ptr<std::atomic<uint64_t>[]> bucket_counts;
    std::atomic<uint64_t> sum{0};
    std::atomic<uint64_t> count{0};
    size_t size = 0;

    HistogramStorage() = default;

    HistogramStorage(size_t bucket_size):
        size(bucket_size + 1) {
        bucket_counts = std::make_unique<std::atomic<uint64_t>[]>(size);
        for (size_t i = 0; i < size; ++i) {
            bucket_counts[i].store(0, std::memory_order_relaxed);
        }
    }

    HistogramStorage(HistogramStorage&& other) noexcept {
        bucket_counts = std::move(other.bucket_counts);
        sum.store(other.sum.load(std::memory_order_relaxed), std::memory_order_relaxed);
        count.store(other.count.load(std::memory_order_relaxed), std::memory_order_relaxed);
        size = other.size;
        other.size = 0;
    }

    HistogramStorage& operator=(HistogramStorage&& other) noexcept {
        if (this != &other) {
            bucket_counts = std::move(other.bucket_counts);
            sum.store(other.sum.load(std::memory_order_relaxed), std::memory_order_relaxed);
            count.store(other.count.load(std::memory_order_relaxed), std::memory_order_relaxed);
            size = other.size;
            other.size = 0;
        }
        return *this;
    }
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_METRICS_METRICS_INTERNAL_H
