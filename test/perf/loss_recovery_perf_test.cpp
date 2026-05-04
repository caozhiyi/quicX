// =============================================================================
// loss_recovery_perf_test.cpp - Loss detection / RTT / ACK processing bench
// =============================================================================
//
// Exercises the ack-processing and rtt-estimation path that QUIC walks for
// every ACK frame.  We intentionally avoid SendControl here because it
// depends on a live timer plant; instead we drive the underlying
// RttCalculator and the CC ack-bulk path directly.  If SendControl is later
// refactored to be testable in isolation, this is the natural place to add
// that suite.
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "quic/congestion_control/congestion_control_factory.h"
#include "quic/connection/controler/rtt_calculator.h"

namespace quicx {
namespace perf {

// ===========================================================================
// Scenario 1: RttCalculator::UpdateRtt throughput
// ===========================================================================
// Called at least once per ACK frame.  Usually a few times per ACK if the
// frame covers multiple newly acked packets.

static void BM_Recovery_RttUpdate(benchmark::State& state) {
    quic::RttCalculator rtt;

    // Pre-seed with one sample so subsequent UpdateRtt hits the steady path.
    rtt.UpdateRtt(/*send_time=*/0, /*now=*/100, /*ack_delay=*/5);

    uint64_t send_t = 1;
    uint64_t now = 200;
    for (auto _ : state) {
        bool ok = rtt.UpdateRtt(send_t, now, /*ack_delay=*/5);
        benchmark::DoNotOptimize(ok);
        ++send_t;
        now += 100;
    }
    state.SetItemsProcessed(state.iterations());
}

static void BM_Recovery_RttGetters(benchmark::State& state) {
    quic::RttCalculator rtt;
    rtt.UpdateRtt(0, 100, 5);
    rtt.UpdateRtt(10, 220, 5);
    rtt.UpdateRtt(20, 340, 5);

    for (auto _ : state) {
        uint32_t a = rtt.GetLatestRtt();
        uint32_t b = rtt.GetSmoothedRtt();
        uint32_t c = rtt.GetMinRtt();
        uint32_t d = rtt.GetRttVar();
        uint32_t e = rtt.GetPT0Interval(/*max_ack_delay=*/25);
        benchmark::DoNotOptimize(a);
        benchmark::DoNotOptimize(b);
        benchmark::DoNotOptimize(c);
        benchmark::DoNotOptimize(d);
        benchmark::DoNotOptimize(e);
    }
    state.SetItemsProcessed(state.iterations());
}

// ===========================================================================
// Scenario 2: Heavy-ACK burst through the CC
// ===========================================================================
// On a high-bandwidth link, a single ACK frame often acks 50-100 packets.
// This measures how the CC copes with that burst (the most common hot path
// in loss recovery).

static void BM_Recovery_AckBurst(benchmark::State& state) {
    auto type = static_cast<quic::CongestionControlType>(state.range(0));
    const int burst = static_cast<int>(state.range(1));

    for (auto _ : state) {
        auto cc = quic::CreateCongestionControl(type);
        if (!cc) { state.SkipWithError("create cc failed"); return; }

        quic::CcConfigV2 cfg;
        cfg.initial_cwnd_bytes = 32 * 1460;
        cfg.mss_bytes = 1460;
        cc->Configure(cfg);

        // Fill the flight.
        for (int i = 1; i <= burst; ++i) {
            cc->OnPacketSent({(uint64_t)i, 1460, (uint64_t)(i * 100), false});
        }

        // Now ack all of them in a single burst.
        for (int i = 1; i <= burst; ++i) {
            cc->OnPacketAcked(
                {(uint64_t)i, 1460, (uint64_t)(burst * 100 + 50), /*ack_delay=*/5, false});
        }

        benchmark::DoNotOptimize(cc->GetCongestionWindow());
        benchmark::DoNotOptimize(cc->GetBytesInFlight());
    }
    state.SetItemsProcessed(state.iterations() * burst);
}

// ===========================================================================
// Scenario 3: Loss burst through the CC
// ===========================================================================
// A classic "tail loss" pattern: several packets in flight, then N losses in
// a row.  Tests how cheaply each CC handles the back-to-back loss events.

static void BM_Recovery_LossBurst(benchmark::State& state) {
    auto type = static_cast<quic::CongestionControlType>(state.range(0));
    const int losses = static_cast<int>(state.range(1));

    for (auto _ : state) {
        auto cc = quic::CreateCongestionControl(type);
        if (!cc) { state.SkipWithError("create cc failed"); return; }

        quic::CcConfigV2 cfg;
        cfg.initial_cwnd_bytes = 64 * 1460;
        cfg.mss_bytes = 1460;
        cc->Configure(cfg);

        for (int i = 1; i <= losses; ++i) {
            cc->OnPacketSent({(uint64_t)i, 1460, (uint64_t)(i * 100), false});
        }
        for (int i = 1; i <= losses; ++i) {
            cc->OnPacketLost({(uint64_t)i, 1460, (uint64_t)(losses * 100 + 200)});
        }

        benchmark::DoNotOptimize(cc->GetCongestionWindow());
    }
    state.SetItemsProcessed(state.iterations() * losses);
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Registration
// ===========================================================================

BENCHMARK(quicx::perf::BM_Recovery_RttUpdate)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Recovery_RttGetters)->Unit(benchmark::kNanosecond);

BENCHMARK(quicx::perf::BM_Recovery_AckBurst)
    ->ArgsProduct({
        {static_cast<int>(quicx::quic::CongestionControlType::kCubic),
         static_cast<int>(quicx::quic::CongestionControlType::kReno),
         static_cast<int>(quicx::quic::CongestionControlType::kBbrV1),
         static_cast<int>(quicx::quic::CongestionControlType::kBbrV2),
         static_cast<int>(quicx::quic::CongestionControlType::kBbrV3)},
        {16, 64, 256}
    })
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(quicx::perf::BM_Recovery_LossBurst)
    ->ArgsProduct({
        {static_cast<int>(quicx::quic::CongestionControlType::kCubic),
         static_cast<int>(quicx::quic::CongestionControlType::kReno),
         static_cast<int>(quicx::quic::CongestionControlType::kBbrV1),
         static_cast<int>(quicx::quic::CongestionControlType::kBbrV2),
         static_cast<int>(quicx::quic::CongestionControlType::kBbrV3)},
        {4, 16, 64}
    })
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();

#else
int main() { return 0; }
#endif
