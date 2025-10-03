#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>
#include <vector>
#include <cstring>

#include "common/buffer/buffer.h"

namespace quicx {
namespace common {

static std::shared_ptr<Buffer> MakeBuffer(size_t cap = 64 * 1024) {
    auto mem = std::make_shared<std::vector<uint8_t>>(cap);
    return std::make_shared<Buffer>(mem->data(), (uint32_t)mem->size());
}

static void BM_Buffer_WriteRead_Move(benchmark::State& state) {
    const size_t payload = static_cast<size_t>(state.range(0));
    std::vector<uint8_t> src(payload, 0xAB);

    for (auto _ : state) {
        auto buf = MakeBuffer(payload * 2 + 16);
        // write
        benchmark::DoNotOptimize(buf->Write(src.data(), (uint32_t)src.size()));
        // read no move
        std::vector<uint8_t> tmp(payload);
        benchmark::DoNotOptimize(buf->ReadNotMovePt(tmp.data(), (uint32_t)tmp.size()));
        // move read
        benchmark::DoNotOptimize(buf->Read(tmp.data(), (uint32_t)tmp.size()));
        // write again
        benchmark::DoNotOptimize(buf->Write(src.data(), (uint32_t)src.size()));
        // move write pointer back partially
        benchmark::DoNotOptimize(buf->MoveWritePt(-(int32_t)(payload / 2)));
    }
}

} // namespace common
} // namespace quicx

BENCHMARK(quicx::common::BM_Buffer_WriteRead_Move)->Arg(256)->Arg(1024)->Arg(4096)->Arg(16384);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


