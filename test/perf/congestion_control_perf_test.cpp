// =============================================================================
// congestion_control_perf_test.cpp - CC algorithm CPU micro-benchmarks
// =============================================================================
//
// Runs an identical event stream (sent / acked / lost) through every
// congestion-control implementation shipped by quicX, plus the NormalPacer,
// and reports per-event latency.  This gives an apples-to-apples comparison
// of Cubic vs Reno vs BBRv1/v2/v3 so the default can be chosen on data.
//
// Build / usage:
//   cmake -B build -DENABLE_PERF_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
//   cmake --build build --target congestion_control_perf_test -j
//   ./build/bin/perf/congestion_control_perf_test
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "quic/congestion_control/congestion_control_factory.h"
#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/normal_pacer.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Shared event-stream generator
// ===========================================================================
//
// We generate a deterministic "typical" flight pattern:
//   - Steady stream of sent packets of kMss bytes
//   - ACKs piggybacked every kAckEvery events
//   - A loss every kLossEvery events
// This matches what the CC controller sees in steady state and measures the
// per-event CPU (not the decisions it makes).

static constexpr uint64_t kMss = 1460;
static constexpr uint64_t kInitCwnd = 10 * kMss;

struct Event {
    enum class Kind { kSent, kAcked, kLost };
    Kind kind;
    uint64_t pn;
    uint64_t bytes;
    uint64_t time_us;
};

static std::vector<Event> BuildEventStream(size_t n_events) {
    std::vector<Event> out;
    out.reserve(n_events);

    constexpr size_t kAckEvery = 2;   // ACK every 2 sends
    constexpr size_t kLossEvery = 128;  // 1 loss per ~100 sends

    uint64_t pn = 0;
    uint64_t t = 0;
    for (size_t i = 0; i < n_events; ++i) {
        if (i % kLossEvery == (kLossEvery - 1)) {
            out.push_back({Event::Kind::kLost, pn, kMss, t});
        } else if (i % kAckEvery == 0) {
            out.push_back({Event::Kind::kAcked, pn, kMss, t});
        } else {
            out.push_back({Event::Kind::kSent, ++pn, kMss, t});
        }
        t += 100;  // 100us between events
    }
    return out;
}

static void ConfigureCc(quic::ICongestionControl* cc) {
    quic::CcConfigV2 cfg;
    cfg.initial_cwnd_bytes = kInitCwnd;
    cfg.min_cwnd_bytes = 2 * kMss;
    cfg.max_cwnd_bytes = 1000 * kMss;
    cfg.mss_bytes = kMss;
    cc->Configure(cfg);
}

// ===========================================================================
// Scenario 1: OnPacketSent latency
// ===========================================================================

static void BM_Cc_OnPacketSent(benchmark::State& state) {
    auto type = static_cast<quic::CongestionControlType>(state.range(0));
    auto cc = quic::CreateCongestionControl(type);
    if (!cc) { state.SkipWithError("CC create failed"); return; }
    ConfigureCc(cc.get());

    uint64_t pn = 0;
    uint64_t t = 0;
    for (auto _ : state) {
        quic::SentPacketEvent ev{++pn, kMss, t += 100, /*is_retransmit=*/false};
        cc->OnPacketSent(ev);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Scenario 2: End-to-end event-stream processing
// ===========================================================================
//
// Runs a fixed batch of kStreamLen mixed events through the CC and measures
// total CPU.  Reports items_per_second as events/sec.

static void BM_Cc_EventStream(benchmark::State& state) {
    auto type = static_cast<quic::CongestionControlType>(state.range(0));
    constexpr size_t kStreamLen = 4096;
    auto events = BuildEventStream(kStreamLen);

    for (auto _ : state) {
        auto cc = quic::CreateCongestionControl(type);
        if (!cc) { state.SkipWithError("CC create failed"); return; }
        ConfigureCc(cc.get());

        for (const auto& e : events) {
            switch (e.kind) {
                case Event::Kind::kSent:
                    cc->OnPacketSent({e.pn, e.bytes, e.time_us, false});
                    break;
                case Event::Kind::kAcked:
                    cc->OnPacketAcked({e.pn, e.bytes, e.time_us, /*ack_delay=*/0, /*ecn_ce=*/false});
                    break;
                case Event::Kind::kLost:
                    cc->OnPacketLost({e.pn, e.bytes, e.time_us});
                    break;
            }
        }

        // Force observable side-effects so the compiler can't DCE the loop.
        benchmark::DoNotOptimize(cc->GetCongestionWindow());
        benchmark::DoNotOptimize(cc->GetBytesInFlight());
    }
    state.SetItemsProcessed(state.iterations() * kStreamLen);
}

// ===========================================================================
// Scenario 3: CanSend query in the send loop
// ===========================================================================

static void BM_Cc_CanSend(benchmark::State& state) {
    auto type = static_cast<quic::CongestionControlType>(state.range(0));
    auto cc = quic::CreateCongestionControl(type);
    if (!cc) { state.SkipWithError("CC create failed"); return; }
    ConfigureCc(cc.get());

    // Put some bytes in flight so CanSend has work to do.
    for (uint64_t i = 1; i <= 32; ++i) {
        cc->OnPacketSent({i, kMss, i * 100, false});
    }

    uint64_t now = 100000;
    for (auto _ : state) {
        uint64_t allow = 0;
        auto r = cc->CanSend(now, allow);
        benchmark::DoNotOptimize(r);
        benchmark::DoNotOptimize(allow);
        now += 10;
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Scenario 4: NormalPacer
// ===========================================================================

static void BM_Pacer_CanSend_TimeUntilSend(benchmark::State& state) {
    quic::NormalPacer pacer;
    pacer.OnPacingRateUpdated(/*bytes per second*/ 100ull * 1024 * 1024);

    uint64_t now = 100000;
    for (auto _ : state) {
        bool can = pacer.CanSend(now);
        uint64_t tt = pacer.TimeUntilSend();
        pacer.OnPacketSent(now, kMss);
        benchmark::DoNotOptimize(can);
        benchmark::DoNotOptimize(tt);
        now += 1;
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Registration
// ===========================================================================
//
// Google benchmark macros embed __LINE__ as a unique id, so every BENCHMARK
// invocation must live on its own source line.

#define REGISTER_CC_VARIANTS(bm)                                        \
    BENCHMARK(bm)                                                       \
        ->Arg(static_cast<int>(quicx::quic::CongestionControlType::kCubic)) \
        ->Arg(static_cast<int>(quicx::quic::CongestionControlType::kReno))  \
        ->Arg(static_cast<int>(quicx::quic::CongestionControlType::kBbrV1)) \
        ->Arg(static_cast<int>(quicx::quic::CongestionControlType::kBbrV2)) \
        ->Arg(static_cast<int>(quicx::quic::CongestionControlType::kBbrV3)) \
        ->Unit(benchmark::kNanosecond)

REGISTER_CC_VARIANTS(quicx::perf::BM_Cc_OnPacketSent);
REGISTER_CC_VARIANTS(quicx::perf::BM_Cc_EventStream);
REGISTER_CC_VARIANTS(quicx::perf::BM_Cc_CanSend);

BENCHMARK(quicx::perf::BM_Pacer_CanSend_TimeUntilSend)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();

#else
int main() { return 0; }
#endif
