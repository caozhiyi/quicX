#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <cstdint>

#include "common/timer/timer_task.h"
#include "common/timer/treemap_timer.h"


namespace quicx {
namespace common {

static void BM_TreeMapTimer_AddRm(benchmark::State& state) {
    TreeMapTimer t;
    TimerTask task;
    task.SetTimeoutCallback([](){});
    for (auto _ : state) {
        uint64_t id = t.AddTimer(task, /*time_ms*/10);
        benchmark::DoNotOptimize(id);
        t.RmTimer(task);
    }
}

static void BM_TreeMapTimer_Run(benchmark::State& state) {
    TreeMapTimer t;
    TimerTask task;
    task.SetTimeoutCallback([](){});
    // add many timers
    for (int i = 0; i < 1000; ++i) {
        t.AddTimer(task, 10 + (i % 10));
    }
    for (auto _ : state) {
        t.TimerRun(0);
    }
}

} // namespace common
} // namespace quicx

BENCHMARK(quicx::common::BM_TreeMapTimer_AddRm);
BENCHMARK(quicx::common::BM_TreeMapTimer_Run);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


