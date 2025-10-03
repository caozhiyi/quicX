#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>
#include <vector>

#include "http3/qpack/qpack_encoder.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace http3 {

static std::shared_ptr<common::Buffer> MakeBuffer(size_t cap = 4096) {
    auto mem = std::make_shared<std::vector<uint8_t>>(cap);
    return std::make_shared<common::Buffer>(mem->data(), (uint32_t)mem->size());
}

static void BM_Qpack_Instr_InsertWithoutNameRef(benchmark::State& state) {
    QpackEncoder enc;
    std::vector<std::pair<std::string,std::string>> inserts = {{"x-k", std::string((size_t)state.range(0), 'v')}};
    for (auto _ : state) {
        auto ctrl = MakeBuffer();
        bool ok = enc.EncodeEncoderInstructions(inserts, ctrl, /*with_name_ref*/false);
        benchmark::DoNotOptimize(ok);
    }
}

static void BM_Qpack_Instr_InsertWithStaticNameRef(benchmark::State& state) {
    QpackEncoder enc;
    std::vector<std::pair<std::string,std::string>> inserts = {{":method", std::string((size_t)state.range(0), 'G')}};
    for (auto _ : state) {
        auto ctrl = MakeBuffer();
        bool ok = enc.EncodeEncoderInstructions(inserts, ctrl, /*with_name_ref*/true);
        benchmark::DoNotOptimize(ok);
    }
}

static void BM_Qpack_Instr_Duplicate(benchmark::State& state) {
    QpackEncoder enc;
    // Pre-insert one entry to allow duplicate of newest (rel=0)
    auto ctrl0 = MakeBuffer();
    enc.EncodeEncoderInstructions({{"x-d", "v"}}, ctrl0, false);
    for (auto _ : state) {
        auto ctrl = MakeBuffer();
        bool ok = enc.EncodeEncoderInstructions({}, ctrl, /*with_name_ref*/false, /*set_capacity*/false, /*cap*/0, /*duplicate_index*/0);
        benchmark::DoNotOptimize(ok);
    }
}

} // namespace http3
} // namespace quicx

BENCHMARK(quicx::http3::BM_Qpack_Instr_InsertWithoutNameRef)->Arg(8)->Arg(64)->Arg(512);
BENCHMARK(quicx::http3::BM_Qpack_Instr_InsertWithStaticNameRef)->Arg(1);
BENCHMARK(quicx::http3::BM_Qpack_Instr_Duplicate);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif



