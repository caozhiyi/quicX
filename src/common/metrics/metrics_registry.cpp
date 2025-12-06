#include <algorithm>

#include "common/metrics/metrics_registry.h"

namespace quicx {
namespace common {

// --- GlobalRegistry Implementation ---

GlobalRegistry* GlobalRegistry::Instance() {
    static GlobalRegistry instance;
    return &instance;
}

MetricID GlobalRegistry::Register(const std::string& name, const std::string& help,
    const std::map<std::string, std::string>& labels, MetricType type, const std::vector<uint64_t>& buckets) {
    std::lock_guard<std::mutex> lock(mutex_);
    MetricKey key{name, labels};
    auto it = key_to_id_.find(key);
    if (it != key_to_id_.end()) {
        return it->second;
    }

    MetricID id = next_id_++;
    MetricMeta meta;
    meta.id = id;
    meta.type = type;
    meta.name = name;
    meta.help = help;
    meta.labels = labels;
    meta.buckets = buckets;

    metas_.push_back(meta);
    key_to_id_[key] = id;
    return id;
}

const MetricMeta* GlobalRegistry::GetMeta(MetricID id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (id < metas_.size()) {
        return &metas_[id];
    }
    return nullptr;
}

void GlobalRegistry::RegisterThread(ThreadMetricStorage* storage) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_threads_.push_back(storage);
}

void GlobalRegistry::UnregisterThread(ThreadMetricStorage* storage) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find(active_threads_.begin(), active_threads_.end(), storage);
    if (it != active_threads_.end()) {
        active_threads_.erase(it);
    }

    if (dead_counters_.size() < storage->counters_.size()) {
        dead_counters_.resize(storage->counters_.size(), 0);
    }
    if (dead_gauges_.size() < storage->gauges_.size()) {
        dead_gauges_.resize(storage->gauges_.size(), 0);
    }
    if (dead_histograms_.size() < storage->histograms_.size()) {
        dead_histograms_.resize(storage->histograms_.size());
    }

    for (size_t i = 0; i < storage->counters_.size(); ++i) {
        if (storage->counters_[i]) {
            dead_counters_[i] += storage->counters_[i]->load(std::memory_order_relaxed);
        }
    }
    for (size_t i = 0; i < storage->gauges_.size(); ++i) {
        if (storage->gauges_[i]) {
            dead_gauges_[i] += storage->gauges_[i]->load(std::memory_order_relaxed);
        }
    }
    for (size_t i = 0; i < storage->histograms_.size(); ++i) {
        if (storage->histograms_[i].size > 0) {
            auto& src = storage->histograms_[i];
            auto& dest = dead_histograms_[i];

            dest.sum += src.sum.load(std::memory_order_relaxed);
            dest.count += src.count.load(std::memory_order_relaxed);

            if (dest.bucket_counts.size() < src.size) {
                dest.bucket_counts.resize(src.size, 0);
            }
            for (size_t b = 0; b < src.size; ++b) {
                dest.bucket_counts[b] += src.bucket_counts[b].load(std::memory_order_relaxed);
            }
        }
    }
}

void GlobalRegistry::Collect(std::vector<uint64_t>& counters, std::vector<int64_t>& gauges,
    std::vector<std::unique_ptr<HistogramStorage>>& histograms) {
    std::lock_guard<std::mutex> lock(mutex_);

    counters = dead_counters_;
    gauges = dead_gauges_;

    size_t max_id = metas_.size();
    if (counters.size() < max_id) {
        counters.resize(max_id, 0);
    }
    if (gauges.size() < max_id) {
        gauges.resize(max_id, 0);
    }
    if (histograms.size() < max_id) {
        histograms.resize(max_id);
    }

    // Copy dead histograms
    for (size_t i = 0; i < dead_histograms_.size() && i < max_id; ++i) {
        if (dead_histograms_[i].bucket_counts.empty()) {
            continue;
        }

        if (!histograms[i]) {
            histograms[i] = std::make_unique<HistogramStorage>(dead_histograms_[i].bucket_counts.size() - 1);
        }

        histograms[i]->sum.store(dead_histograms_[i].sum, std::memory_order_relaxed);
        histograms[i]->count.store(dead_histograms_[i].count, std::memory_order_relaxed);
        for (size_t b = 0; b < dead_histograms_[i].bucket_counts.size(); ++b) {
            histograms[i]->bucket_counts[b].store(dead_histograms_[i].bucket_counts[b], std::memory_order_relaxed);
        }
    }

    // Aggregate active threads
    for (auto* storage : active_threads_) {
        for (size_t i = 0; i < storage->counters_.size(); ++i) {
            if (storage->counters_[i]) {
                if (i >= counters.size()) {
                    counters.resize(i + 1, 0);
                }
                counters[i] += storage->counters_[i]->load(std::memory_order_relaxed);
            }
        }
        for (size_t i = 0; i < storage->gauges_.size(); ++i) {
            if (storage->gauges_[i]) {
                if (i >= gauges.size()) {
                    gauges.resize(i + 1, 0);
                }
                gauges[i] += storage->gauges_[i]->load(std::memory_order_relaxed);
            }
        }
        for (size_t i = 0; i < storage->histograms_.size(); ++i) {
            if (storage->histograms_[i].size > 0) {
                if (i >= histograms.size()) {
                    histograms.resize(i + 1);
                }

                auto& src = storage->histograms_[i];
                if (!histograms[i]) {
                    histograms[i] = std::make_unique<HistogramStorage>(src.size - 1);
                }
                auto& dest = *histograms[i];

                dest.sum.fetch_add(src.sum.load(std::memory_order_relaxed), std::memory_order_relaxed);
                dest.count.fetch_add(src.count.load(std::memory_order_relaxed), std::memory_order_relaxed);

                for (size_t b = 0; b < src.size; ++b) {
                    dest.bucket_counts[b].fetch_add(
                        src.bucket_counts[b].load(std::memory_order_relaxed), std::memory_order_relaxed);
                }
            }
        }
    }
}

// --- ThreadMetricStorage Implementation ---

ThreadMetricStorage::ThreadMetricStorage() {
    GlobalRegistry::Instance()->RegisterThread(this);
}

ThreadMetricStorage::~ThreadMetricStorage() {
    GlobalRegistry::Instance()->UnregisterThread(this);
}

std::atomic<uint64_t>& ThreadMetricStorage::GetCounter(MetricID id) {
    if (id >= counters_.size()) {
        GrowCounters(id + 1);
    }
    return *counters_[id];
}

std::atomic<int64_t>& ThreadMetricStorage::GetGauge(MetricID id) {
    if (id >= gauges_.size()) {
        GrowGauges(id + 1);
    }
    return *gauges_[id];
}

HistogramStorage& ThreadMetricStorage::GetHistogram(MetricID id, const std::vector<uint64_t>& buckets) {
    if (id >= histograms_.size()) {
        GrowHistograms(id + 1);
    }
    if (histograms_[id].size == 0) {
        histograms_[id] = HistogramStorage(buckets.size());
    }
    return histograms_[id];
}

void ThreadMetricStorage::GrowCounters(size_t size) {
    size_t old_size = counters_.size();
    counters_.resize(size);
    for (size_t i = old_size; i < size; ++i) {
        counters_[i] = std::make_unique<std::atomic<uint64_t>>(0);
    }
}

void ThreadMetricStorage::GrowGauges(size_t size) {
    size_t old_size = gauges_.size();
    gauges_.resize(size);
    for (size_t i = old_size; i < size; ++i) {
        gauges_[i] = std::make_unique<std::atomic<int64_t>>(0);
    }
}

void ThreadMetricStorage::GrowHistograms(size_t size) {
    histograms_.resize(size);
}

// --- Thread-local storage accessor ---

ThreadMetricStorage& GetThreadStorage() {
    thread_local ThreadMetricStorage storage;
    return storage;
}

}  // namespace common
}  // namespace quicx
