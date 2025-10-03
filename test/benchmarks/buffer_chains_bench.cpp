#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>
#include <vector>

#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_chains.h"

namespace quicx {
namespace common {

static std::shared_ptr<BufferChains> MakeChains(uint32_t block_size, uint32_t add_num) {
    auto pool = MakeBlockMemoryPoolPtr(block_size, add_num);
    return std::make_shared<BufferChains>(pool);
}

// Append many small chunks and then flatten by reading
static void BM_BufferChains_AppendAndFlatten(benchmark::State& state) {
    const uint32_t block_size = 4096;
    auto chains = MakeChains(block_size, 128);
    const size_t chunk = static_cast<size_t>(state.range(0));
    const size_t total = static_cast<size_t>(state.range(1));
    std::vector<uint8_t> data(chunk, 0xCD);

    for (auto _ : state) {
        // reset
        chains = MakeChains(block_size, 128);
        // append
        size_t appended = 0;
        while (appended < total) {
            appended += chains->Write(data.data(), (uint32_t)std::min(chunk, total - appended));
        }
        // flatten by reading out
        std::vector<uint8_t> sink(total);
        uint32_t read = chains->Read(sink.data(), (uint32_t)total);
        benchmark::DoNotOptimize(read);
        benchmark::DoNotOptimize(sink.data());
    }
}

} // namespace common
} // namespace quicx

BENCHMARK(quicx::common::BM_BufferChains_AppendAndFlatten)
    ->Args({64, 4096})
    ->Args({256, 64 * 1024})
    ->Args({1024, 4 * 1024 * 1024});
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


