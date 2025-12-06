#include <sstream>

#include "common/metrics/metrics.h"
#include "common/metrics/metrics_internal.h"
#include "common/metrics/metrics_registry.h"
#include "common/metrics/metrics_std.h"

namespace quicx {
namespace common {

// Global enable flag for fast check
static bool g_metrics_enabled = false;

// --- Metrics Class Implementation ---

bool Metrics::Initialize(const MetricsConfig& config) {
    g_metrics_enabled = config.enable;

    if (g_metrics_enabled) {
        // Initialize all standard metrics
        InitializeStandardMetrics();
    }

    return true;
}

MetricID Metrics::RegisterCounter(
    const std::string& name, const std::string& help, const std::map<std::string, std::string>& labels) {
    if (!g_metrics_enabled) return kInvalidMetricID;
    return GlobalRegistry::Instance()->Register(name, help, labels, MetricType::kCounter);
}

MetricID Metrics::RegisterGauge(
    const std::string& name, const std::string& help, const std::map<std::string, std::string>& labels) {
    if (!g_metrics_enabled) return kInvalidMetricID;
    return GlobalRegistry::Instance()->Register(name, help, labels, MetricType::kGauge);
}

MetricID Metrics::RegisterHistogram(const std::string& name, const std::string& help,
    const std::vector<uint64_t>& buckets, const std::map<std::string, std::string>& labels) {
    if (!g_metrics_enabled) return kInvalidMetricID;
    return GlobalRegistry::Instance()->Register(name, help, labels, MetricType::kHistogram, buckets);
}

void Metrics::CounterInc(MetricID id, uint64_t val) {
    if (!g_metrics_enabled || id == kInvalidMetricID) return;
    GetThreadStorage().GetCounter(id).fetch_add(val, std::memory_order_relaxed);
}

void Metrics::GaugeInc(MetricID id, int64_t val) {
    if (!g_metrics_enabled || id == kInvalidMetricID) return;
    GetThreadStorage().GetGauge(id).fetch_add(val, std::memory_order_relaxed);
}

void Metrics::GaugeDec(MetricID id, int64_t val) {
    if (!g_metrics_enabled || id == kInvalidMetricID) return;
    GetThreadStorage().GetGauge(id).fetch_sub(val, std::memory_order_relaxed);
}

void Metrics::GaugeSet(MetricID id, int64_t val) {
    if (!g_metrics_enabled || id == kInvalidMetricID) return;
    GetThreadStorage().GetGauge(id).store(val, std::memory_order_relaxed);
}

void Metrics::HistogramObserve(MetricID id, uint64_t val) {
    if (!g_metrics_enabled || id == kInvalidMetricID) return;

    auto* meta = GlobalRegistry::Instance()->GetMeta(id);
    if (!meta || meta->type != MetricType::kHistogram) return;

    auto& storage = GetThreadStorage().GetHistogram(id, meta->buckets);

    storage.sum.fetch_add(val, std::memory_order_relaxed);
    storage.count.fetch_add(1, std::memory_order_relaxed);

    size_t i = 0;
    for (; i < meta->buckets.size(); ++i) {
        if (val <= meta->buckets[i]) {
            break;
        }
    }
    storage.bucket_counts[i].fetch_add(1, std::memory_order_relaxed);
}

std::string Metrics::ExportPrometheus() {
    if (!g_metrics_enabled) return "";

    std::vector<uint64_t> counters;
    std::vector<int64_t> gauges;
    std::vector<std::unique_ptr<HistogramStorage>> histograms;

    GlobalRegistry::Instance()->Collect(counters, gauges, histograms);

    std::stringstream ss;
    auto* registry = GlobalRegistry::Instance();

    auto format_labels = [](const std::map<std::string, std::string>& labels) -> std::string {
        if (labels.empty()) return "";
        std::stringstream lss;
        lss << "{";
        bool first = true;
        for (const auto& pair : labels) {
            if (!first) lss << ",";
            lss << pair.first << "=\"" << pair.second << "\"";
            first = false;
        }
        lss << "}";
        return lss.str();
    };

    auto format_labels_extra = [](const std::map<std::string, std::string>& labels, const std::string& k,
                                   const std::string& v) -> std::string {
        std::stringstream lss;
        lss << "{";
        bool first = true;
        for (const auto& pair : labels) {
            if (!first) lss << ",";
            lss << pair.first << "=\"" << pair.second << "\"";
            first = false;
        }
        if (!first) lss << ",";
        lss << k << "=\"" << v << "\"";
        lss << "}";
        return lss.str();
    };

    size_t max_id = counters.size();
    if (gauges.size() > max_id) max_id = gauges.size();
    if (histograms.size() > max_id) max_id = histograms.size();

    for (MetricID id = 0; id < max_id; ++id) {
        auto* meta = registry->GetMeta(id);
        if (!meta) continue;

        if (meta->type == MetricType::kCounter) {
            if (id < counters.size()) {
                ss << "# HELP " << meta->name << " " << meta->help << "\n";
                ss << "# TYPE " << meta->name << " counter\n";
                ss << meta->name << format_labels(meta->labels) << " " << counters[id] << "\n";
            }
        } else if (meta->type == MetricType::kGauge) {
            if (id < gauges.size()) {
                ss << "# HELP " << meta->name << " " << meta->help << "\n";
                ss << "# TYPE " << meta->name << " gauge\n";
                ss << meta->name << format_labels(meta->labels) << " " << gauges[id] << "\n";
            }
        } else if (meta->type == MetricType::kHistogram) {
            if (id < histograms.size() && histograms[id]) {
                ss << "# HELP " << meta->name << " " << meta->help << "\n";
                ss << "# TYPE " << meta->name << " histogram\n";

                auto& h = *histograms[id];
                uint64_t cumulative = 0;
                for (size_t i = 0; i < meta->buckets.size(); ++i) {
                    cumulative += h.bucket_counts[i].load(std::memory_order_relaxed);
                    ss << meta->name << "_bucket"
                       << format_labels_extra(meta->labels, "le", std::to_string(meta->buckets[i])) << " " << cumulative
                       << "\n";
                }
                // +Inf
                if (meta->buckets.size() < h.size) {
                    cumulative += h.bucket_counts[meta->buckets.size()].load(std::memory_order_relaxed);
                }
                ss << meta->name << "_bucket" << format_labels_extra(meta->labels, "le", "+Inf") << " " << cumulative
                   << "\n";

                ss << meta->name << "_sum" << format_labels(meta->labels) << " "
                   << h.sum.load(std::memory_order_relaxed) << "\n";
                ss << meta->name << "_count" << format_labels(meta->labels) << " "
                   << h.count.load(std::memory_order_relaxed) << "\n";
            }
        }
    }

    return ss.str();
}

}  // namespace common
}  // namespace quicx
