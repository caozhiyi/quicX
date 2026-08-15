#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <vector>

#include <quicx/common/if_timer_scheduler.h>

#include "common/timer/timer_core.h"
#include "common/timer/timer_task.h"
#include "common/timer/treemap_timer.h"
#include "common/util/time.h"

// ============================================================
//  Timer Benchmark Suite
//
//  Compares the production engine (TimerCore: hierarchical timing wheel + slab
//  handle table) against TreeMapTimer, which is kept purely as a reference
//  implementation and baseline. The wheel is only worth its complexity if it
//  beats a red-black tree, and an earlier wheel implementation did NOT: its
//  O(1) algorithm was buried under an id->iterator shadow index that cost two
//  heap allocations and a random-keyed hash insert per timer. These benchmarks
//  exist to keep that from silently coming back.
//
//  Scenarios:
//    1. Arm + cancel (single timer, hot loop)
//    2. Rearm in place -- the per-packet PTO / idle-reset path
//    3. Bulk arm (N timers)
//    4. Fire N expired timers
//    5. MinTime query (runs once per event loop iteration)
//    6. Scatter: realistic mix of short/medium/long delays
//    7. Mixed tick: advance, fire, re-arm
// ============================================================

namespace quicx {
namespace common {
namespace {

// Arm a timer on the core and wrap it in the handle callers actually use.
inline Timer ArmHandle(std::shared_ptr<TimerCore> core, uint32_t delay_ms, uint64_t now) {
    uint32_t gen = 0;
    uint32_t index = core->Arm([]() {}, {}, /*has_owner=*/false, delay_ms, /*interval_ms=*/0, now, gen);
    return Timer(core, index, gen);
}

}  // namespace

// ────────────────────────────────────────────────────────────
// 1. Arm + cancel (single timer, repeated)
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_AddRemove(benchmark::State& state) {
    TreeMapTimer timer;
    TimerTask task;
    task.SetTimeoutCallback([]() {});
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        timer.AddTimer(task, 100, now);
        benchmark::DoNotOptimize(task.GetId());
        timer.RemoveTimer(task);
    }
}
BENCHMARK(BM_TreeMap_AddRemove);

static void BM_Core_AddRemove(benchmark::State& state) {
    auto core = std::make_shared<TimerCore>();
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        Timer t = ArmHandle(core, 100, now);
        benchmark::DoNotOptimize(t.IsActive());
        t.Cancel();
    }
}
BENCHMARK(BM_Core_AddRemove);

// ────────────────────────────────────────────────────────────
// 2. Reschedule an existing timer.
//
// This is the shape of the hottest timer path in the library: every outgoing
// packet pushes the PTO deadline out, and every incoming packet pushes the idle
// deadline out. TreeMap has no way to express it other than remove + insert
// (two node allocations); TimerCore splices the existing node to its new slot.
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_Reschedule(benchmark::State& state) {
    TreeMapTimer timer;
    TimerTask task;
    task.SetTimeoutCallback([]() {});
    uint64_t now = UTCTimeMsec();
    timer.AddTimer(task, 100, now);
    uint32_t i = 0;
    for (auto _ : state) {
        timer.RemoveTimer(task);
        timer.AddTimer(task, 100 + (++i % 8), now);
    }
}
BENCHMARK(BM_TreeMap_Reschedule);

static void BM_Core_Rearm(benchmark::State& state) {
    auto core = std::make_shared<TimerCore>();
    uint64_t now = UTCTimeMsec();
    Timer t = ArmHandle(core, 100, now);
    uint32_t i = 0;
    for (auto _ : state) {
        benchmark::DoNotOptimize(t.Rearm(100 + (++i % 8), now));
    }
}
BENCHMARK(BM_Core_Rearm);

// ────────────────────────────────────────────────────────────
// 3. Bulk arm (N timers, state.range(0) = N)
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_BulkAdd(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TreeMapTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([]() {});
        state.ResumeTiming();

        for (int i = 0; i < N; ++i) {
            timer.AddTimer(tasks[i], static_cast<uint32_t>(10 + (i % 500)), now);
        }
        benchmark::DoNotOptimize(timer.Empty());
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_TreeMap_BulkAdd)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Core_BulkAdd(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        auto core = std::make_shared<TimerCore>();
        std::vector<Timer> handles;
        handles.reserve(N);
        state.ResumeTiming();

        for (int i = 0; i < N; ++i) {
            handles.push_back(ArmHandle(core, static_cast<uint32_t>(10 + (i % 500)), now));
        }
        benchmark::DoNotOptimize(core->Empty());

        state.PauseTiming();
        handles.clear();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_Core_BulkAdd)->Arg(100)->Arg(1000)->Arg(10000);

// ────────────────────────────────────────────────────────────
// 4. Fire N expired timers
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_RunFire(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TreeMapTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([]() {});
        for (int i = 0; i < N; ++i) {
            timer.AddTimer(tasks[i], 0, now);  // expires at `now`
        }
        state.ResumeTiming();

        timer.TimerRun(now);  // fires all N
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_TreeMap_RunFire)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Core_RunFire(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        auto core = std::make_shared<TimerCore>();
        // Detached timers: no handle, which is what PostDelayed uses and what
        // makes this measure firing rather than handle bookkeeping.
        for (int i = 0; i < N; ++i) {
            core->ArmDetached([]() {}, 0, now);
        }
        state.ResumeTiming();

        core->Run(now);  // fires all N
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_Core_RunFire)->Arg(100)->Arg(1000)->Arg(10000);

// ────────────────────────────────────────────────────────────
// 5. MinTime: the next-expiry query the event loop makes every iteration
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_MinTime(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    TreeMapTimer timer;
    uint64_t now = UTCTimeMsec();
    std::vector<TimerTask> tasks(N);
    for (int i = 0; i < N; ++i) {
        tasks[i].SetTimeoutCallback([]() {});
        timer.AddTimer(tasks[i], static_cast<uint32_t>(10 + (i % 500)), now);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(timer.MinTime(now));
    }
}
BENCHMARK(BM_TreeMap_MinTime)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Core_MinTime(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    auto core = std::make_shared<TimerCore>();
    uint64_t now = UTCTimeMsec();
    for (int i = 0; i < N; ++i) {
        core->ArmDetached([]() {}, static_cast<uint32_t>(10 + (i % 500)), now);
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(core->MinTime(now));
    }
}
BENCHMARK(BM_Core_MinTime)->Arg(100)->Arg(1000)->Arg(10000);

// ────────────────────────────────────────────────────────────
// 6. Scatter: mixed short / medium / long delays
//    Simulates a realistic QUIC timer workload:
//      ~50% short(1–20 ms   → wheel L0)
//      ~30% medium (20–500 ms → wheel L0/L1)
//      ~20% long   (500 ms – 60 s → wheel L1/L2)
// ────────────────────────────────────────────────────────────

static uint32_t ScatterDelay(int i) {
    int r = i % 10;
    if (r < 5) {
        return static_cast<uint32_t>(1 + (i % 20));  // 1-20 ms
    }
    if (r < 8) {
        return static_cast<uint32_t>(20 + (i % 480));  // 20-499 ms
    }
    return static_cast<uint32_t>(500 + (i % 59500));  // 500-59999 ms
}

static void BM_TreeMap_Scatter(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        TreeMapTimer timer;
        std::vector<TimerTask> tasks(N);
        for (auto& t : tasks) t.SetTimeoutCallback([]() {});
        state.ResumeTiming();

        for (int i = 0; i < N; ++i) {
            timer.AddTimer(tasks[i], ScatterDelay(i), now);
        }
        benchmark::DoNotOptimize(timer.MinTime(now));
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_TreeMap_Scatter)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Core_Scatter(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    for (auto _ : state) {
        state.PauseTiming();
        auto core = std::make_shared<TimerCore>();
        state.ResumeTiming();

        for (int i = 0; i < N; ++i) {
            core->ArmDetached([]() {}, ScatterDelay(i), now);
        }
        benchmark::DoNotOptimize(core->MinTime(now));
    }
    state.SetItemsProcessed(state.iterations() * N);
}
BENCHMARK(BM_Core_Scatter)->Arg(100)->Arg(1000)->Arg(10000);

// ────────────────────────────────────────────────────────────
// 7. Mixed tick: advance the clock, fire, re-arm. The event-loop pattern.
// ────────────────────────────────────────────────────────────

static void BM_TreeMap_MixedTick(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    TreeMapTimer timer;
    std::vector<TimerTask> tasks(N);
    for (auto& t : tasks) t.SetTimeoutCallback([]() {});
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
BENCHMARK(BM_TreeMap_MixedTick)->Arg(100)->Arg(1000)->Arg(10000);

static void BM_Core_MixedTick(benchmark::State& state) {
    const int N = static_cast<int>(state.range(0));
    uint64_t now = UTCTimeMsec();
    auto core = std::make_shared<TimerCore>();
    for (int i = 0; i < N; ++i) {
        core->ArmDetached([]() {}, static_cast<uint32_t>(5 + (i % 20)), now);
    }

    for (auto _ : state) {
        now += 10;
        core->Run(now);
        for (int i = 0; i < N / 10; ++i) {
            core->ArmDetached([]() {}, static_cast<uint32_t>(5 + (i % 20)), now);
        }
        benchmark::DoNotOptimize(core->MinTime(now));
    }
}
BENCHMARK(BM_Core_MixedTick)->Arg(100)->Arg(1000)->Arg(10000);

}  // namespace common
}  // namespace quicx

BENCHMARK_MAIN();

#else
int main() {
    return 0;
}
#endif
