#if defined(QUICX_ENABLE_BENCHMARKS)
// Benchmarks comparing the various smart-pointer / pool APIs exposed by
// AlloterWrap, plus a std::make_shared baseline. The goal is to quantify the
// effect of the recent if_alloter.h refactor:
//   - path A: stateless deleter for the legacy PoolNewSharePtr (now routed to
//             PoolMakeShared internally);
//   - path B: PoolMakeShared via std::allocate_shared + PoolStdAllocator,
//             which co-allocates the control block AND the object inside the
//             pool in a single shot.
//
// All cases construct/destroy the same trivial 64-byte object so any timing
// delta is owed to the allocation strategy, not to user code.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "common/alloter/if_alloter.h"
#include "common/alloter/pool_alloter.h"

namespace quicx {
namespace common {

// 64-byte payload object. Picked to:
//   - stay well below PoolAlloter's 256B cutoff even after ~40B of control-block
//     metadata (so PoolMakeShared exercises the actual free-list fast path);
//   - be representative of a small QUIC frame.
struct Payload64 {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t c = 0;
    uint64_t d = 0;
    uint64_t e = 0;
    uint64_t f = 0;
    uint64_t g = 0;
    uint64_t h = 0;

    Payload64() = default;
    explicit Payload64(uint64_t v) : a(v), b(v), c(v), d(v), e(v), f(v), g(v), h(v) {}
};
static_assert(sizeof(Payload64) == 64, "Payload64 must stay at 64 bytes");

// ---------------------------------------------------------------------------
// 1. Baseline: std::make_shared (combined control-block + object alloc, but
//    served by the global operator new, NOT the pool).
// ---------------------------------------------------------------------------
static void BM_StdMakeShared(benchmark::State& state) {
    for (auto _ : state) {
        auto sp = std::make_shared<Payload64>(0xDEADBEEFu);
        benchmark::DoNotOptimize(sp.get());
    }
}
BENCHMARK(BM_StdMakeShared);

// 2. Baseline: raw new/delete (no smart pointer at all).
static void BM_RawNewDelete(benchmark::State& state) {
    for (auto _ : state) {
        auto* p = new Payload64(0xDEADBEEFu);
        benchmark::DoNotOptimize(p);
        delete p;
    }
}
BENCHMARK(BM_RawNewDelete);

// ---------------------------------------------------------------------------
// 3. Pool: PoolNew + PoolDelete (raw pointer, manual lifetime, fastest).
// ---------------------------------------------------------------------------
static void BM_PoolNewDelete(benchmark::State& state) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    for (auto _ : state) {
        auto* p = wrap.PoolNew<Payload64>(0xDEADBEEFu);
        benchmark::DoNotOptimize(p);
        wrap.PoolDelete<Payload64>(p);
    }
}
BENCHMARK(BM_PoolNewDelete);

// 4. Pool: PoolMakeUnique (RAII unique_ptr, stateless deleter).
static void BM_PoolMakeUnique(benchmark::State& state) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    for (auto _ : state) {
        auto up = wrap.PoolMakeUnique<Payload64>(0xDEADBEEFu);
        benchmark::DoNotOptimize(up.get());
    }
}
BENCHMARK(BM_PoolMakeUnique);

// 5. Pool: PoolMakeShared (new path B — control block + object both pooled
//    in a single allocation via std::allocate_shared).
static void BM_PoolMakeShared(benchmark::State& state) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    for (auto _ : state) {
        auto sp = wrap.PoolMakeShared<Payload64>(0xDEADBEEFu);
        benchmark::DoNotOptimize(sp.get());
    }
}
BENCHMARK(BM_PoolMakeShared);

// 6. Pool: legacy PoolNewSharePtr (now internally forwards to PoolMakeShared,
//    so this should match BM_PoolMakeShared closely — measured to confirm
//    the rewrite preserved performance and to baseline against any future
//    regressions).
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
static void BM_PoolNewSharePtr_Legacy(benchmark::State& state) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    for (auto _ : state) {
        auto sp = wrap.PoolNewSharePtr<Payload64>(0xDEADBEEFu);
        benchmark::DoNotOptimize(sp.get());
    }
}
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif
BENCHMARK(BM_PoolNewSharePtr_Legacy);

// ---------------------------------------------------------------------------
// 7. shared_ptr COPY cost: stresses the atomic refcount path. The control-block
//    location (heap vs. pool) does NOT affect copy speed (refcount is in the
//    control block either way), so this should be ~identical for std::shared
//    and PoolMakeShared. Useful as a sanity check.
// ---------------------------------------------------------------------------
static void BM_StdSharedPtr_Copy(benchmark::State& state) {
    auto sp = std::make_shared<Payload64>(0xDEADBEEFu);
    for (auto _ : state) {
        auto cp = sp;  // atomic inc + atomic dec on destruction
        benchmark::DoNotOptimize(cp.get());
    }
}
BENCHMARK(BM_StdSharedPtr_Copy);

static void BM_PoolMakeShared_Copy(benchmark::State& state) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    auto sp = wrap.PoolMakeShared<Payload64>(0xDEADBEEFu);
    for (auto _ : state) {
        auto cp = sp;
        benchmark::DoNotOptimize(cp.get());
    }
}
BENCHMARK(BM_PoolMakeShared_Copy);

// ---------------------------------------------------------------------------
// 8. Bulk allocation patterns (more realistic than alloc/free in lockstep,
//    which is the cheapest possible workload because every iter hits the
//    same free-list bucket head). Allocate N, then release N — exercises
//    the pool free-list under sustained pressure.
// ---------------------------------------------------------------------------
static constexpr int kBulk = 64;

static void BM_StdMakeShared_Bulk(benchmark::State& state) {
    std::vector<std::shared_ptr<Payload64>> v;
    v.reserve(kBulk);
    for (auto _ : state) {
        v.clear();
        for (int i = 0; i < kBulk; ++i) {
            v.emplace_back(std::make_shared<Payload64>(static_cast<uint64_t>(i)));
        }
        benchmark::DoNotOptimize(v.data());
    }
}
BENCHMARK(BM_StdMakeShared_Bulk);

static void BM_PoolMakeShared_Bulk(benchmark::State& state) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    std::vector<std::shared_ptr<Payload64>> v;
    v.reserve(kBulk);
    for (auto _ : state) {
        v.clear();
        for (int i = 0; i < kBulk; ++i) {
            v.emplace_back(wrap.PoolMakeShared<Payload64>(static_cast<uint64_t>(i)));
        }
        benchmark::DoNotOptimize(v.data());
    }
}
BENCHMARK(BM_PoolMakeShared_Bulk);

static void BM_PoolMakeUnique_Bulk(benchmark::State& state) {
    AlloterWrap wrap(std::shared_ptr<IAlloter>(new PoolAlloter()));
    std::vector<PoolUniquePtr<Payload64>> v;
    v.reserve(kBulk);
    for (auto _ : state) {
        v.clear();
        for (int i = 0; i < kBulk; ++i) {
            v.emplace_back(wrap.PoolMakeUnique<Payload64>(static_cast<uint64_t>(i)));
        }
        benchmark::DoNotOptimize(v.data());
    }
}
BENCHMARK(BM_PoolMakeUnique_Bulk);

}  // namespace common
}  // namespace quicx

BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif
