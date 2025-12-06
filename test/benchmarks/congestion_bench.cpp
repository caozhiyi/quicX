#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/reno_congestion_control.h"

namespace quicx {
namespace quic {

static std::unique_ptr<ICongestionControl> MakeReno() {
    return std::make_unique<RenoCongestionControl>();
}

static void BM_Congestion_OnAck(benchmark::State& state) {
    auto cc = MakeReno();
    CcConfigV2 cfg;
    cc->Configure(cfg);
    // simulate N packets sent
    for (uint64_t i = 0; i < 10000; ++i) {
        cc->OnPacketSent({i, 1200, i});
    }
    for (auto _ : state) {
        // ack a window worth of packets per iteration
        for (uint64_t i = 0; i < 1000; ++i) {
            cc->OnPacketAcked({i, 1200, i});
        }
    }
}

static void BM_Congestion_OnLoss(benchmark::State& state) {
    auto cc = MakeReno();
    CcConfigV2 cfg;
    cc->Configure(cfg);
    for (uint64_t i = 0; i < 10000; ++i) {
        cc->OnPacketSent({i, 1200, i});
    }
    for (auto _ : state) {
        for (uint64_t i = 0; i < 1000; ++i) {
            cc->OnPacketLost({i, 1200, i});
        }
    }
}

}  // namespace quic
}  // namespace quicx

BENCHMARK(quicx::quic::BM_Congestion_OnAck);
BENCHMARK(quicx::quic::BM_Congestion_OnLoss);
BENCHMARK_MAIN();
#else
int main() {
    return 0;
}
#endif
