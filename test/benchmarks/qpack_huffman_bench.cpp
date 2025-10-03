#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <string>

#include "http3/qpack/huffman_encoder.h"

namespace quicx {
namespace http3 {

static void BM_Qpack_Huffman_Encode(benchmark::State& state) {
    HuffmanEncoder enc;
    const std::string s(static_cast<size_t>(state.range(0)), 'a');
    for (auto _ : state) {
        auto out = enc.Encode(s);
        benchmark::DoNotOptimize(out);
    }
}

static void BM_Qpack_Huffman_Decode(benchmark::State& state) {
    HuffmanEncoder enc;
    const std::string s(static_cast<size_t>(state.range(0)), 'a');
    auto out = enc.Encode(s);
    for (auto _ : state) {
        auto dec = enc.Decode(out);
        benchmark::DoNotOptimize(dec);
    }
}

} // namespace http3
} // namespace quicx

BENCHMARK(quicx::http3::BM_Qpack_Huffman_Encode)->Arg(8)->Arg(64)->Arg(1024);
BENCHMARK(quicx::http3::BM_Qpack_Huffman_Decode)->Arg(8)->Arg(64)->Arg(1024);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


