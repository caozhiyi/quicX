#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <cstdlib>

#include "common/qlog/qlog_manager.h"
#include "common/qlog/qlog_trace.h"
#include "common/qlog/writer/async_writer.h"
#include "common/qlog/event/transport_events.h"
#include "common/qlog/event/recovery_events.h"
#include "common/qlog/event/connectivity_events.h"
#include "common/qlog/util/qlog_constants.h"
#include "common/util/time.h"

namespace quicx {
namespace common {

// ========== Helpers ==========

static std::string BenchTempDir() {
    auto dir = std::filesystem::temp_directory_path() / "quicx_bench_qlog";
    std::filesystem::create_directories(dir);
    return dir.string();
}

static void CleanupBenchTempDir() {
    auto dir = std::filesystem::temp_directory_path() / "quicx_bench_qlog";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

static QlogConfig MakeBenchConfig() {
    QlogConfig cfg;
    cfg.enabled = true;
    cfg.output_dir = BenchTempDir();
    cfg.format = QlogFileFormat::kSequential;
    cfg.async_queue_size = 100000;
    cfg.flush_interval_ms = 50;
    cfg.batch_write = true;
    cfg.sampling_rate = 1.0f;
    return cfg;
}

static PacketSentData MakePacketSentData(uint64_t pkt_num) {
    PacketSentData data;
    data.packet_number = pkt_num;
    data.packet_type = quic::k1RttPacketType;
    data.packet_size = 1200;
    data.frames.push_back(quic::kStream);
    data.frames.push_back(quic::kAck);
    return data;
}

static RecoveryMetricsData MakeRecoveryMetricsData() {
    RecoveryMetricsData data;
    data.min_rtt_us = 5000;
    data.smoothed_rtt_us = 8000;
    data.latest_rtt_us = 7500;
    data.rtt_variance_us = 1000;
    data.cwnd_bytes = 65535;
    data.bytes_in_flight = 32000;
    data.ssthresh = 32768;
    data.pacing_rate_bps = 100000000;
    return data;
}

static ConnectionStartedData MakeConnectionStartedData() {
    ConnectionStartedData data;
    data.src_ip = "192.168.1.100";
    data.src_port = 12345;
    data.dst_ip = "10.0.0.1";
    data.dst_port = 443;
    data.src_cid = "abcdef0123456789";
    data.dst_cid = "9876543210fedcba";
    data.protocol = "QUIC";
    data.ip_version = "ipv4";
    return data;
}

// ========== Benchmark 1: Single Event Record Latency ==========

// Measures the time to log a single PacketSent event (end-to-end: serialize + enqueue)
static void BM_Qlog_SingleEventLatency_PacketSent(benchmark::State& state) {
    auto cfg = MakeBenchConfig();
    AsyncWriter writer(cfg);
    writer.Start();

    QlogTrace trace("bench-pkt-sent", VantagePoint::kClient, cfg);
    trace.SetWriter(&writer);

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        auto data = MakePacketSentData(pkt_num++);
        trace.LogPacketSent(UTCTimeMsec() * 1000, data);
    }

    writer.Flush();
    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations());
}

// Measures the time to log a single RecoveryMetrics event
static void BM_Qlog_SingleEventLatency_RecoveryMetrics(benchmark::State& state) {
    auto cfg = MakeBenchConfig();
    AsyncWriter writer(cfg);
    writer.Start();

    QlogTrace trace("bench-recovery", VantagePoint::kServer, cfg);
    trace.SetWriter(&writer);

    for (auto _ : state) {
        auto data = MakeRecoveryMetricsData();
        trace.LogMetricsUpdated(UTCTimeMsec() * 1000, data);
    }

    writer.Flush();
    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations());
}

// Measures the time to log a single ConnectionStarted event
static void BM_Qlog_SingleEventLatency_ConnectionStarted(benchmark::State& state) {
    auto cfg = MakeBenchConfig();
    AsyncWriter writer(cfg);
    writer.Start();

    QlogTrace trace("bench-conn-start", VantagePoint::kClient, cfg);
    trace.SetWriter(&writer);

    for (auto _ : state) {
        auto data = MakeConnectionStartedData();
        trace.LogConnectionStarted(UTCTimeMsec() * 1000, data);
    }

    writer.Flush();
    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations());
}

// ========== Benchmark 2: Event Throughput ==========

// Measures sustained throughput of PacketSent events (events per second)
static void BM_Qlog_Throughput_PacketSent(benchmark::State& state) {
    const int64_t batch_size = state.range(0);
    auto cfg = MakeBenchConfig();
    AsyncWriter writer(cfg);
    writer.Start();

    QlogTrace trace("bench-throughput", VantagePoint::kClient, cfg);
    trace.SetWriter(&writer);

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        for (int64_t i = 0; i < batch_size; ++i) {
            auto data = MakePacketSentData(pkt_num++);
            trace.LogPacketSent(UTCTimeMsec() * 1000, data);
        }
    }

    writer.Flush();
    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations() * batch_size);
}

// Mixed event throughput (simulates realistic workload)
static void BM_Qlog_Throughput_MixedEvents(benchmark::State& state) {
    const int64_t batch_size = state.range(0);
    auto cfg = MakeBenchConfig();
    AsyncWriter writer(cfg);
    writer.Start();

    QlogTrace trace("bench-mixed", VantagePoint::kClient, cfg);
    trace.SetWriter(&writer);

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        for (int64_t i = 0; i < batch_size; ++i) {
            uint64_t ts = UTCTimeMsec() * 1000;
            if (i % 10 == 0) {
                // Every 10th event: recovery metrics
                auto metrics = MakeRecoveryMetricsData();
                trace.LogMetricsUpdated(ts, metrics);
            } else {
                // Normal: packet_sent
                auto data = MakePacketSentData(pkt_num++);
                trace.LogPacketSent(ts, data);
            }
        }
    }

    writer.Flush();
    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations() * batch_size);
}

// ========== Benchmark 3: AsyncWriter Queue Pressure ==========

// Measures behavior under high event rate (producer faster than writer can flush)
static void BM_Qlog_AsyncWriter_QueuePressure(benchmark::State& state) {
    const int64_t burst_size = state.range(0);
    auto cfg = MakeBenchConfig();
    cfg.flush_interval_ms = 200;  // Slower flush to build up queue
    AsyncWriter writer(cfg);
    writer.Start();

    QlogTrace trace("bench-pressure", VantagePoint::kClient, cfg);
    trace.SetWriter(&writer);

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        // Burst write
        for (int64_t i = 0; i < burst_size; ++i) {
            auto data = MakePacketSentData(pkt_num++);
            trace.LogPacketSent(UTCTimeMsec() * 1000, data);
        }
        // Force flush after burst
        writer.Flush();
    }

    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations() * burst_size);
    state.counters["total_events"] = benchmark::Counter(
        static_cast<double>(writer.GetTotalEventsWritten()),
        benchmark::Counter::kDefaults);
    state.counters["total_bytes"] = benchmark::Counter(
        static_cast<double>(writer.GetTotalBytesWritten()),
        benchmark::Counter::kDefaults);
}

// ========== Benchmark 4: Sampling Rate Performance Comparison ==========

// Sampling rate 1.0 (log everything)
static void BM_Qlog_SamplingRate_100Percent(benchmark::State& state) {
    auto cfg = MakeBenchConfig();
    cfg.sampling_rate = 1.0f;
    AsyncWriter writer(cfg);
    writer.Start();

    QlogTrace trace("bench-sample-100", VantagePoint::kClient, cfg);
    trace.SetWriter(&writer);

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        auto data = MakePacketSentData(pkt_num++);
        trace.LogPacketSent(UTCTimeMsec() * 1000, data);
    }

    writer.Flush();
    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations());
}

// Sampling rate 0.5 via event whitelist filtering (trace exists but half events filtered)
static void BM_Qlog_EventFilter_Whitelist(benchmark::State& state) {
    auto cfg = MakeBenchConfig();
    // Only allow recovery events, filter out packet events
    cfg.event_whitelist = {"recovery:metrics_updated"};
    AsyncWriter writer(cfg);
    writer.Start();

    QlogTrace trace("bench-filter", VantagePoint::kClient, cfg);
    trace.SetWriter(&writer);

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        // These will be filtered out by whitelist (fast-path rejection)
        auto data = MakePacketSentData(pkt_num++);
        trace.LogPacketSent(UTCTimeMsec() * 1000, data);
    }

    writer.Flush();
    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations());
}

// No-op when qlog trace is nullptr (simulates sampling_rate = 0)
static void BM_Qlog_SamplingRate_NullTrace(benchmark::State& state) {
    std::shared_ptr<QlogTrace> trace = nullptr;

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        // Simulates QLOG_PACKET_SENT(trace, data) with null trace
        if (trace) {
            auto data = MakePacketSentData(pkt_num++);
            trace->LogPacketSent(UTCTimeMsec() * 1000, data);
        }
    }

    state.SetItemsProcessed(state.iterations());
}

// ========== Benchmark 5: Serialization Overhead ==========

// Measure pure serialization cost (ToJson) without I/O
static void BM_Qlog_Serialization_PacketSentToJson(benchmark::State& state) {
    auto data = MakePacketSentData(12345);
    for (auto _ : state) {
        std::string json = data.ToJson();
        benchmark::DoNotOptimize(json);
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_Qlog_Serialization_RecoveryMetricsToJson(benchmark::State& state) {
    auto data = MakeRecoveryMetricsData();
    for (auto _ : state) {
        std::string json = data.ToJson();
        benchmark::DoNotOptimize(json);
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_Qlog_Serialization_ConnectionStartedToJson(benchmark::State& state) {
    auto data = MakeConnectionStartedData();
    for (auto _ : state) {
        std::string json = data.ToJson();
        benchmark::DoNotOptimize(json);
    }
    state.SetItemsProcessed(state.iterations());
}

// ========== Benchmark 6: QlogManager CreateTrace/RemoveTrace ==========

static void BM_Qlog_Manager_CreateRemoveTrace(benchmark::State& state) {
    auto& mgr = QlogManager::Instance();
    auto cfg = MakeBenchConfig();
    mgr.SetConfig(cfg);

    uint64_t conn_idx = 0;
    for (auto _ : state) {
        std::string cid = "bench-mgr-" + std::to_string(conn_idx++);
        auto trace = mgr.CreateTrace(cid, VantagePoint::kClient);
        benchmark::DoNotOptimize(trace);
        mgr.RemoveTrace(cid);
    }

    // Cleanup
    cfg.enabled = false;
    mgr.SetConfig(cfg);
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations());
}

// ========== Benchmark 7: Multi-Connection Concurrent Write ==========

static void BM_Qlog_MultiConnection_Write(benchmark::State& state) {
    const int64_t num_connections = state.range(0);
    auto cfg = MakeBenchConfig();
    AsyncWriter writer(cfg);
    writer.Start();

    // Create multiple traces
    std::vector<std::unique_ptr<QlogTrace>> traces;
    traces.reserve(num_connections);
    for (int64_t i = 0; i < num_connections; ++i) {
        auto trace = std::make_unique<QlogTrace>(
            "bench-multi-" + std::to_string(i), VantagePoint::kClient, cfg);
        trace->SetWriter(&writer);
        traces.push_back(std::move(trace));
    }

    uint64_t pkt_num = 0;
    for (auto _ : state) {
        // Round-robin write across all connections
        for (int64_t i = 0; i < num_connections; ++i) {
            auto data = MakePacketSentData(pkt_num++);
            traces[i]->LogPacketSent(UTCTimeMsec() * 1000, data);
        }
    }

    writer.Flush();
    traces.clear();
    writer.Stop();
    CleanupBenchTempDir();

    state.SetItemsProcessed(state.iterations() * num_connections);
}

}  // namespace common
}  // namespace quicx

// ========== Register Benchmarks ==========

// 1. Single event latency (nanosecond-level)
BENCHMARK(quicx::common::BM_Qlog_SingleEventLatency_PacketSent);
BENCHMARK(quicx::common::BM_Qlog_SingleEventLatency_RecoveryMetrics);
BENCHMARK(quicx::common::BM_Qlog_SingleEventLatency_ConnectionStarted);

// 2. Throughput (events per second)
BENCHMARK(quicx::common::BM_Qlog_Throughput_PacketSent)
    ->Arg(100)->Arg(1000)->Arg(10000);
BENCHMARK(quicx::common::BM_Qlog_Throughput_MixedEvents)
    ->Arg(100)->Arg(1000)->Arg(10000);

// 3. AsyncWriter queue pressure
BENCHMARK(quicx::common::BM_Qlog_AsyncWriter_QueuePressure)
    ->Arg(1000)->Arg(5000)->Arg(10000);

// 4. Sampling rate comparison
BENCHMARK(quicx::common::BM_Qlog_SamplingRate_100Percent);
BENCHMARK(quicx::common::BM_Qlog_EventFilter_Whitelist);
BENCHMARK(quicx::common::BM_Qlog_SamplingRate_NullTrace);

// 5. Serialization overhead (pure CPU, no I/O)
BENCHMARK(quicx::common::BM_Qlog_Serialization_PacketSentToJson);
BENCHMARK(quicx::common::BM_Qlog_Serialization_RecoveryMetricsToJson);
BENCHMARK(quicx::common::BM_Qlog_Serialization_ConnectionStartedToJson);

// 6. Manager trace lifecycle
BENCHMARK(quicx::common::BM_Qlog_Manager_CreateRemoveTrace);

// 7. Multi-connection write
BENCHMARK(quicx::common::BM_Qlog_MultiConnection_Write)
    ->Arg(1)->Arg(5)->Arg(10)->Arg(50);

BENCHMARK_MAIN();
#else
int main() {
    return 0;
}
#endif
