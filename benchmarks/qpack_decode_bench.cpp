#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>
#include <vector>
#include <unordered_map>

#include "http3/qpack/qpack_encoder.h"
#include "http3/qpack/static_table.h"
#include "http3/qpack/util.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace http3 {

static std::shared_ptr<common::Buffer> MakeBuffer(size_t cap = 4096) {
    auto mem = std::make_shared<std::vector<uint8_t>>(cap);
    return std::make_shared<common::Buffer>(mem->data(), (uint32_t)mem->size());
}

static void BM_Qpack_Decode_StaticIndexed(benchmark::State& state) {
    QpackEncoder dec;
    // Choose a known static entry index
    int32_t sidx = StaticTable::Instance().FindHeaderItemIndex(":method", "GET");
    if (sidx < 0) sidx = 17; // fallback to typical index if lookup fails

    for (auto _ : state) {
        auto hdr = MakeBuffer();
        dec.WriteHeaderPrefix(hdr, /*ric*/0, /*base*/0);
        QpackEncodePrefixedInteger(hdr, 6, 0xC0, static_cast<uint64_t>(sidx));
        std::unordered_map<std::string, std::string> headers;
        bool ok = dec.Decode(hdr, headers);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(headers);
    }
}

static void BM_Qpack_Decode_DynamicIndexed(benchmark::State& state) {
    QpackEncoder enc;
    QpackEncoder dec;
    // Warm up: insert dynamic entry
    auto ctrl = MakeBuffer();
    std::vector<std::pair<std::string,std::string>> inserts = {{"x-bench", "v"}};
    enc.EncodeEncoderInstructions(inserts, ctrl);
    dec.DecodeEncoderInstructions(ctrl);

    for (auto _ : state) {
        auto hdr = MakeBuffer();
        dec.WriteHeaderPrefix(hdr, /*ric*/1, /*base*/1);
        QpackEncodePrefixedInteger(hdr, 6, 0x80, 0);
        std::unordered_map<std::string, std::string> headers;
        bool ok = dec.Decode(hdr, headers);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(headers);
    }
}

static void BM_Qpack_Decode_Literal_NoIndex(benchmark::State& state) {
    QpackEncoder dec;
    const std::string name = "x-lit";
    const std::string value(static_cast<size_t>(state.range(0)), 'a');

    for (auto _ : state) {
        auto hdr = MakeBuffer();
        dec.WriteHeaderPrefix(hdr, /*ric*/0, /*base*/0);
        uint8_t literal = 0x20;
        hdr->Write(&literal, 1);
        QpackEncodeStringLiteral(name, hdr, false);
        QpackEncodeStringLiteral(value, hdr, false);
        std::unordered_map<std::string, std::string> headers;
        bool ok = dec.Decode(hdr, headers);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(headers);
    }
}

} // namespace http3
} // namespace quicx

BENCHMARK(quicx::http3::BM_Qpack_Decode_StaticIndexed);
BENCHMARK(quicx::http3::BM_Qpack_Decode_DynamicIndexed);
BENCHMARK(quicx::http3::BM_Qpack_Decode_Literal_NoIndex)->Arg(8)->Arg(64)->Arg(512);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif


