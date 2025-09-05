#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <cstdint>

#include "quic/connection/controler/rtt_calculator.h"

namespace quicx {
namespace quic {

static void BM_Rtt_Update_And_PTO(benchmark::State& state) {
    RttCalculator rtt;
    uint64_t now = 0;
    for (auto _ : state) {
        // simulate RTT samples
        for (int i = 0; i < 1000; ++i) {
            rtt.UpdateRtt(/*send_time*/now, /*now*/now + 3000 + (i % 100), /*ack_delay*/1000);
            now += 3000;
        }
        auto pto = rtt.GetPT0Interval(/*max_ack_delay*/1000);
        benchmark::DoNotOptimize(pto);
    }
}

} // namespace quic
} // namespace quicx

BENCHMARK(quicx::quic::BM_Rtt_Update_And_PTO);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


