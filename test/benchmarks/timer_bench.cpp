#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <cstdint>
#include <vector>

#include "common/timer/timer_task.h"
#include "common/timer/timing_wheel_timer.h"
#include "common/timer/treemap_timer.h"
#include "common/util/time.h"

// ============================================================
//  Timer Benchmark Suite
//  Compares TimingWheelTimer vs TreeMapTimer across:
//    1. AddTimer + RemoveTimer (single task, hot loop)
//    2. AddTimer bulk (N tasks, no remove)
//    3. TimerRun (fire N expired timers)
//    4. Mixed workload: add -> advance -> fire -> re-add
//    5. Scatter: N tasks with varied delays (short / medium / long)
// ============================================================

namespace quicx {
namespace common {

// ────────────────────────────────────────────────────────────
// 1. AddTimer + RemoveTimer  (single task, repeated)
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_AddRemove(benchmark::State& state) {
    TreeMapTimer timer;
    TimerTask task;
    task.SetTimeoutCallback([](){});
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        timer.AddTimer(task, 100, now);
        benchmark::DoNotOptimize(task.GetId());
        timer.RemoveTimer(task);
    }
}
BENCHMARK(BM_TreeMap_AddRemove);

static void BM_Wheel_AddRemove(benchmark::State& state) {
    TimingWheelTimer timer;
    TimerTask task;
    task.SetTimeoutCallback([](){});
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        timer.AddTimer(task, 100, now);
        benchmark::DoNotOptimize(task.GetId());
        timer.RemoveTimer(task);
    }
}
BENCHMARK(BM_Wheel_AddRemove);

// ────────────────────────────────────────────────────────────
// 2. Bulk AddTimer (N tasks, state.range(0) = N)
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_BulkAdd(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TreeMapTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([](){});
        state.ResumeTiming();

        for (int i = 0; i < N; ++i) {
            timer.AddTimer(tasks[i], static_cast<uint32_t>(10 + (i % 500)), now);
        }
        benchmark::DoNotOptimize(timer.Empty());
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_TreeMap_BulkAdd)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Wheel_BulkAdd(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TimingWheelTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([](){});
        state.ResumeTiming();

        for (int i = 0; i < N; ++i) {
            timer.AddTimer(tasks[i], static_cast<uint32_t>(10 + (i % 500)), now);
        }
        benchmark::DoNotOptimize(timer.Empty());
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_Wheel_BulkAdd)->Arg(100)->Arg(1000)->Arg(10000);

// ────────────────────────────────────────────────────────────
// 3. TimerRun: fire N expired tasks
//    All tasks added with delay=0 so they expire immediately.
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_RunFire(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TreeMapTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([](){});
        for (int i = 0; i < N; ++i) {
            timer.AddTimer(tasks[i], 0, now);   // expires at `now`
        }
        state.ResumeTiming();

        timer.TimerRun(now);   // fires all N
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_TreeMap_RunFire)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Wheel_RunFire(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TimingWheelTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([](){});
        for (int i = 0; i < N; ++i) {
            timer.AddTimer(tasks[i], 0, now);
        }
        state.ResumeTiming();

        timer.TimerRun(now);   // fires all N
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_Wheel_RunFire)->Arg(100)->Arg(1000)->Arg(10000);

// ────────────────────────────────────────────────────────────
// 4. MinTime: query the next expiry time
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_MinTime(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    TreeMapTimer timer;
    uint64_t now = UTCTimeMsec();
    std::vector<TimerTask> tasks(N);
    for (int i = 0; i < N; ++i) {
        tasks[i].SetTimeoutCallback([](){});
        timer.AddTimer(tasks[i], static_cast<uint32_t>(10 + (i % 500)), now);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(timer.MinTime(now));
    }
}
BENCHMARK(BM_TreeMap_MinTime)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Wheel_MinTime(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    TimingWheelTimer timer;
    uint64_t now = UTCTimeMsec();
    std::vector<TimerTask> tasks(N);
    for (int i = 0; i < N; ++i) {
        tasks[i].SetTimeoutCallback([](){});
        timer.AddTimer(tasks[i], static_cast<uint32_t>(10 + (i % 500)), now);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(timer.MinTime(now));
    }
}
BENCHMARK(BM_Wheel_MinTime)->Arg(100)->Arg(1000)->Arg(10000);

// ────────────────────────────────────────────────────────────
// 5. Scatter: mixed short / medium / long delays
//    Simulates a realistic QUIC timer workload:
//      ~50% short  (1–20 ms   → wheel L0)
//      ~30% medium (20–500 ms → wheel L0/L1)
//      ~20% long   (500 ms – 60 s → wheel L1/L2)
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_Scatter(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TreeMapTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([](){});
        state.ResumeTiming();

        for (int i = 0; i < N; ++i) {
            uint32_t delay;
            int r = i % 10;
            if (r < 5)      delay = static_cast<uint32_t>(1  + (i % 20));         // 1-20 ms
            else if (r < 8) delay = static_cast<uint32_t>(20 + (i % 480));        // 20-499 ms
            else            delay = static_cast<uint32_t>(500 + (i % 59500));     // 500-59999 ms
            timer.AddTimer(tasks[i], delay, now);
        }
        benchmark::DoNotOptimize(timer.MinTime(now));
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_TreeMap_Scatter)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Wheel_Scatter(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TimingWheelTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([](){});
        state.ResumeTiming();

        for (int i = 0; i < N; ++i) {
            uint32_t delay;
            int r = i % 10;
            if (r < 5)      delay = static_cast<uint32_t>(1  + (i % 20));
            else if (r < 8) delay = static_cast<uint32_t>(20 + (i % 480));
            else            delay = static_cast<uint32_t>(500 + (i % 59500));
            timer.AddTimer(tasks[i], delay, now);
        }
        benchmark::DoNotOptimize(timer.MinTime(now));
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_Wheel_Scatter)->Arg(100)->Arg(1000)->Arg(10000);

// ────────────────────────────────────────────────────────────
// 6. Mixed: Add N, advance clock by 10ms, fire, re-add
//    Simulates the event-loop tick pattern.
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_MixedTick(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    TreeMapTimer timer;
    std::vector<TimerTask> tasks(N);
    for (auto& t : tasks) t.SetTimeoutCallback([](){});
    for (int i = 0; i < N; ++i) {
        timer.AddTimer(tasks[i], static_cast<uint32_t>(5 + (i % 20)), now);
    }

    for (auto _ : state) {
        now += 10;
        timer.TimerRun(now);
        // Re-add any removed tasks (approximation: just add a fresh one)
        for (int i = 0; i < N / 10; ++i) {
            timer.AddTimer(tasks[i], static_cast<uint32_t>(5 + (i % 20)), now);
        }
        benchmark::DoNotOptimize(timer.MinTime(now));
    }
}
BENCHMARK(BM_TreeMap_MixedTick)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Wheel_MixedTick(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    TimingWheelTimer timer;
    std::vector<TimerTask> tasks(N);
    for (auto& t : tasks) t.SetTimeoutCallback([](){});
    for (int i = 0; i < N; ++i) {
        timer.AddTimer(tasks[i], static_cast<uint32_t>(5 + (i % 20)), now);
    }

    for (auto _ : state) {
        now += 10;
        timer.TimerRun(now);
        for (int i = 0; i < N / 10; ++i) {
            timer.AddTimer(tasks[i], static_cast<uint32_t>(5 + (i % 20)), now);
        }
        benchmark::DoNotOptimize(timer.MinTime(now));
    }
}
BENCHMARK(BM_Wheel_MixedTick)->Arg(100)->Arg(1000)->Arg(10000);

}  // namespace common
}  // namespace quicx

BENCHMARK_MAIN();

#else
int main() { return 0; }
#endif
