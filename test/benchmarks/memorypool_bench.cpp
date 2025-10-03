#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>

#include "common/alloter/pool_block.h"

namespace quicx {
namespace common {

static void BM_BlockMemoryPool_AllocFree(benchmark::State& state) {
    const uint32_t block_size = static_cast<uint32_t>(state.range(0));
    auto pool = MakeBlockMemoryPoolPtr(block_size, /*add_num*/128);
    for (auto _ : state) {
        void* m = pool->PoolLargeMalloc();
        benchmark::DoNotOptimize(m);
        pool->PoolLargeFree(m);
    }
}

} // namespace common
} // namespace quicx

BENCHMARK(quicx::common::BM_BlockMemoryPool_AllocFree)->Arg(2048)->Arg(4096)->Arg(16384);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


