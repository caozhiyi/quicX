#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <vector>

#include "common/decode/decode.h"

namespace quicx {
namespace common {

static void BM_Varint_EncodeDecode(benchmark::State& state) {
    const uint64_t value = static_cast<uint64_t>(state.range(0));
    std::vector<uint8_t> buf(16);
    for (auto _ : state) {
        uint8_t* start = buf.data();
        uint8_t* end = buf.data() + buf.size();
        uint8_t* pos = EncodeVarint(start, end, value);
        benchmark::DoNotOptimize(pos);
        uint64_t out = 0;
        uint8_t* pos2 = DecodeVarint(start, pos, out);
        benchmark::DoNotOptimize(pos2);
        benchmark::DoNotOptimize(out);
    }
}

} // namespace common
} // namespace quicx

BENCHMARK(quicx::common::BM_Varint_EncodeDecode)->Arg(0x3F)->Arg(0x3FFF)->Arg(0x3FFFFFFF)->Arg(0x3FFFFFFFFFFFFFFF);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


