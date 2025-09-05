#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>
#include <vector>
#include <unordered_map>

#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/util.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace http3 {

static std::shared_ptr<common::Buffer> MakeBuffer(size_t cap = 4096) {
    auto mem = std::make_shared<std::vector<uint8_t>>(cap);
    return std::make_shared<common::Buffer>(mem->data(), (uint32_t)mem->size());
}

static void BM_Qpack_Insert_And_IndexedDecode(benchmark::State& state) {
    QpackEncoder enc;
    QpackEncoder dec;

    // Warm up: send one insert instruction
    auto ctrl = MakeBuffer();
    std::vector<std::pair<std::string,std::string>> inserts = {{"x-bench", "v"}};
    enc.EncodeEncoderInstructions(inserts, ctrl);
    dec.DecodeEncoderInstructions(ctrl);

    for (auto _ : state) {
        auto hdr = MakeBuffer();
        dec.WriteHeaderPrefix(hdr, 1, 1);
        QpackEncodePrefixedInteger(hdr, 6, 0x80, 0);
        std::unordered_map<std::string, std::string> headers;
        bool ok = dec.Decode(hdr, headers);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(headers);
    }
}

} // namespace http3
} // namespace quicx

BENCHMARK(quicx::http3::BM_Qpack_Insert_And_IndexedDecode);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


