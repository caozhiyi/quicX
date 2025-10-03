#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>
#include <vector>

#include "quic/frame/stream_frame.h"
#include "common/buffer/buffer.h"

namespace quicx {
namespace quic {

static std::shared_ptr<common::Buffer> MakeBuf(size_t cap=64*1024) {
    auto mem = std::make_shared<std::vector<uint8_t>>(cap);
    return std::make_shared<common::Buffer>(mem->data(), (uint32_t)mem->size());
}

static void BM_StreamFrame_EncodeDecode(benchmark::State& state) {
    const size_t payload = static_cast<size_t>(state.range(0));
    std::vector<uint8_t> data(payload, 0xEF);

    for (auto _ : state) {
        auto buf = MakeBuf(payload + 128);
        StreamFrame f;
        f.SetOffset(0);
        f.SetData(data.data(), (uint32_t)data.size());
        bool enc_ok = f.Encode(buf);
        benchmark::DoNotOptimize(enc_ok);

        auto rd = buf; // reuse as read view
        StreamFrame g;
        bool dec_ok = g.Decode(rd);
        benchmark::DoNotOptimize(dec_ok);
        benchmark::DoNotOptimize(g.GetData());
        benchmark::DoNotOptimize(g.GetLength());
    }
}

} // namespace quic
} // namespace quicx

BENCHMARK(quicx::quic::BM_StreamFrame_EncodeDecode)->Arg(64)->Arg(1024)->Arg(16*1024);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif



