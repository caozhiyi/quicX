#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <thread>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>

#include "common/include/type.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

namespace quicx {
namespace common {

// ===========================================================================
// Setup helper: ensure metrics are initialized once
// ===========================================================================

static void EnsureMetricsInitialized() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        MetricsConfig config;
        config.enable = true;
        Metrics::Initialize(config);
    });
}

// ===========================================================================
// Benchmark 1: Counter Increment Latency (single-thread hot path)
// ===========================================================================

static void BM_Metrics_CounterInc(benchmark::State& state) {
    EnsureMetricsInitialized();
    auto id = Metrics::RegisterCounter("bench_counter_inc", "Benchmark counter");

    for (auto _ : state) {
        Metrics::CounterInc(id);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 2: Counter Increment with Value
// ===========================================================================

static void BM_Metrics_CounterIncWithValue(benchmark::State& state) {
    EnsureMetricsInitialized();
    auto id = Metrics::RegisterCounter("bench_counter_inc_val", "Benchmark counter with value");

    for (auto _ : state) {
        Metrics::CounterInc(id, 42);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 3: Gauge Set Latency
// ===========================================================================

static void BM_Metrics_GaugeSet(benchmark::State& state) {
    EnsureMetricsInitialized();
    auto id = Metrics::RegisterGauge("bench_gauge_set", "Benchmark gauge set");

    int64_t val = 0;
    for (auto _ : state) {
        Metrics::GaugeSet(id, val++);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 4: Gauge Inc/Dec Latency
// ===========================================================================

static void BM_Metrics_GaugeIncDec(benchmark::State& state) {
    EnsureMetricsInitialized();
    auto id = Metrics::RegisterGauge("bench_gauge_incdec", "Benchmark gauge inc/dec");

    for (auto _ : state) {
        Metrics::GaugeInc(id);
        Metrics::GaugeDec(id);
    }
    state.SetItemsProcessed(state.iterations() * 2);
}

// ===========================================================================
// Benchmark 5: Histogram Observe Latency
// ===========================================================================

static void BM_Metrics_HistogramObserve(benchmark::State& state) {
    EnsureMetricsInitialized();
    std::vector<uint64_t> buckets = {100, 500, 1000, 5000, 10000, 50000, 100000};
    auto id = Metrics::RegisterHistogram("bench_histogram", "Benchmark histogram", buckets);

    uint64_t val = 0;
    for (auto _ : state) {
        Metrics::HistogramObserve(id, val % 200000);
        val += 137;  // Spread across buckets
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 6: Histogram Observe with Few Buckets
// ===========================================================================

static void BM_Metrics_HistogramObserve_FewBuckets(benchmark::State& state) {
    EnsureMetricsInitialized();
    std::vector<uint64_t> buckets = {100, 1000};
    auto id = Metrics::RegisterHistogram("bench_hist_few", "Few buckets histogram", buckets);

    uint64_t val = 0;
    for (auto _ : state) {
        Metrics::HistogramObserve(id, val % 2000);
        val += 137;
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 7: Histogram Observe with Many Buckets
// ===========================================================================

static void BM_Metrics_HistogramObserve_ManyBuckets(benchmark::State& state) {
    EnsureMetricsInitialized();
    std::vector<uint64_t> buckets = {10, 25, 50, 75, 100, 150, 200, 300, 500, 750,
                                      1000, 2000, 5000, 10000, 25000, 50000, 100000};
    auto id = Metrics::RegisterHistogram("bench_hist_many", "Many buckets histogram", buckets);

    uint64_t val = 0;
    for (auto _ : state) {
        Metrics::HistogramObserve(id, val % 200000);
        val += 137;
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 8: Standard Metric Operation (realistic: mixed types)
// ===========================================================================

static void BM_Metrics_StandardMetricsMixed(benchmark::State& state) {
    EnsureMetricsInitialized();

    for (auto _ : state) {
        Metrics::CounterInc(MetricsStd::QuicPacketsRx);
        Metrics::CounterInc(MetricsStd::UdpBytesRx, 1200);
        Metrics::GaugeSet(MetricsStd::RttSmoothedUs, 10000);
        Metrics::CounterInc(MetricsStd::FramesRxTotal);
    }
    state.SetItemsProcessed(state.iterations() * 4);
}

// ===========================================================================
// Benchmark 9: Prometheus Export Latency
// ===========================================================================

static void BM_Metrics_ExportPrometheus(benchmark::State& state) {
    EnsureMetricsInitialized();

    // Pre-populate some metrics
    Metrics::CounterInc(MetricsStd::UdpPacketsRx, 10000);
    Metrics::CounterInc(MetricsStd::QuicPacketsTx, 8000);
    Metrics::GaugeSet(MetricsStd::QuicConnectionsActive, 100);
    Metrics::HistogramObserve(MetricsStd::QuicHandshakeDurationUs, 5000);

    for (auto _ : state) {
        std::string output = Metrics::ExportPrometheus();
        benchmark::DoNotOptimize(output);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 10: Disabled Metrics Overhead (fast-path rejection)
// ===========================================================================

static void BM_Metrics_DisabledCounterInc(benchmark::State& state) {
    // Initialize with metrics disabled
    MetricsConfig config;
    config.enable = false;
    Metrics::Initialize(config);

    for (auto _ : state) {
        Metrics::CounterInc(kInvalidMetricID);
    }
    state.SetItemsProcessed(state.iterations());

    // Re-enable for other benchmarks
    config.enable = true;
    Metrics::Initialize(config);
}

// ===========================================================================
// Benchmark 11: Multi-threaded Counter Increment
// ===========================================================================

static void BM_Metrics_MultiThreadCounter(benchmark::State& state) {
    EnsureMetricsInitialized();
    auto id = Metrics::RegisterCounter("bench_mt_counter", "MT counter benchmark");
    const int num_threads = state.range(0);

    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([id]() {
                for (int j = 0; j < 10000; ++j) {
                    Metrics::CounterInc(id);
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    state.SetItemsProcessed(state.iterations() * num_threads * 10000);
}

// ===========================================================================
// Benchmark 12: Multi-threaded Histogram Observe
// ===========================================================================

static void BM_Metrics_MultiThreadHistogram(benchmark::State& state) {
    EnsureMetricsInitialized();
    std::vector<uint64_t> buckets = {100, 500, 1000, 5000, 10000};
    auto id = Metrics::RegisterHistogram("bench_mt_hist", "MT histogram benchmark", buckets);
    const int num_threads = state.range(0);

    for (auto _ : state) {
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([id, i]() {
                for (int j = 0; j < 5000; ++j) {
                    Metrics::HistogramObserve(id, (j * 137 + i * 31) % 20000);
                }
            });
        }
        for (auto& t : threads) {
            t.join();
        }
    }
    state.SetItemsProcessed(state.iterations() * num_threads * 5000);
}

// ===========================================================================
// Benchmark 13: Register Counter (cold path)
// ===========================================================================

static void BM_Metrics_RegisterCounter(benchmark::State& state) {
    EnsureMetricsInitialized();

    uint64_t idx = 0;
    for (auto _ : state) {
        std::string name = "bench_register_" + std::to_string(idx++);
        auto id = Metrics::RegisterCounter(name, "Benchmark register");
        benchmark::DoNotOptimize(id);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 14: Duplicate Registration (cache hit)
// ===========================================================================

static void BM_Metrics_DuplicateRegister(benchmark::State& state) {
    EnsureMetricsInitialized();

    for (auto _ : state) {
        auto id = Metrics::RegisterCounter("bench_dup_register", "Duplicate register");
        benchmark::DoNotOptimize(id);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Benchmark 15: Realistic Packet Processing Metrics Update
// ===========================================================================

static void BM_Metrics_RealisticPacketProcessing(benchmark::State& state) {
    EnsureMetricsInitialized();

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        // Simulate per-packet metrics update (what happens in real packet processing)
        Metrics::CounterInc(MetricsStd::QuicPacketsRx);
        Metrics::CounterInc(MetricsStd::UdpPacketsRx);
        Metrics::CounterInc(MetricsStd::UdpBytesRx, 1200);
        Metrics::CounterInc(MetricsStd::FramesRxTotal, 3);  // ~3 frames per packet
        Metrics::CounterInc(MetricsStd::QuicPacketsAcked);
        pkt_num++;
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace common
}  // namespace quicx

// ========== Register Benchmarks ==========

// Single-thread hot path latency
BENCHMARK(quicx::common::BM_Metrics_CounterInc);
BENCHMARK(quicx::common::BM_Metrics_CounterIncWithValue);
BENCHMARK(quicx::common::BM_Metrics_GaugeSet);
BENCHMARK(quicx::common::BM_Metrics_GaugeIncDec);
BENCHMARK(quicx::common::BM_Metrics_HistogramObserve);
BENCHMARK(quicx::common::BM_Metrics_HistogramObserve_FewBuckets);
BENCHMARK(quicx::common::BM_Metrics_HistogramObserve_ManyBuckets);

// Mixed / realistic
BENCHMARK(quicx::common::BM_Metrics_StandardMetricsMixed);
BENCHMARK(quicx::common::BM_Metrics_RealisticPacketProcessing);

// Export
BENCHMARK(quicx::common::BM_Metrics_ExportPrometheus);

// Fast path (disabled)
BENCHMARK(quicx::common::BM_Metrics_DisabledCounterInc);

// Multi-threaded contention
BENCHMARK(quicx::common::BM_Metrics_MultiThreadCounter)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16);
BENCHMARK(quicx::common::BM_Metrics_MultiThreadHistogram)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8);

// Registration (cold path)
BENCHMARK(quicx::common::BM_Metrics_RegisterCounter);
BENCHMARK(quicx::common::BM_Metrics_DuplicateRegister);

BENCHMARK_MAIN();
#else
int main() {
    return 0;
}
#endif
