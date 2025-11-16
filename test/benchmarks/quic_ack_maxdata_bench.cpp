#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>

#include "quic/frame/ack_frame.h"
#include "quic/frame/max_data_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/multi_block_buffer.h"

namespace quicx {
namespace quic {

static std::shared_ptr<common::IBuffer> MakeBuf(size_t cap=4096) {
    auto pool = std::make_shared<common::BlockMemoryPool>(cap, /*add_num*/128);
    return std::make_shared<common::MultiBlockBuffer>(pool);
}

static void BM_AckFrame_EncodeDecode(benchmark::State& state) {
    for (auto _ : state) {
        AckFrame f;
        f.SetLargestAck(100000);
        f.SetAckDelay(1000);
        f.SetFirstAckRange(10);
        f.AddAckRange(2, 3);
        auto buf = MakeBuf();
        bool ok = f.Encode(buf);
        benchmark::DoNotOptimize(ok);
        AckFrame g;
        ok = g.Decode(buf);
        benchmark::DoNotOptimize(ok);
    }
}

static void BM_MaxDataFrame_EncodeDecode(benchmark::State& state) {
    for (auto _ : state) {
        MaxDataFrame f;
        f.SetMaximumData(10ULL * 1024 * 1024);
        auto buf = MakeBuf();
        bool ok = f.Encode(buf);
        benchmark::DoNotOptimize(ok);
        MaxDataFrame g;
        ok = g.Decode(buf);
        benchmark::DoNotOptimize(ok);
    }
}

} // namespace quic
} // namespace quicx

BENCHMARK(quicx::quic::BM_AckFrame_EncodeDecode);
BENCHMARK(quicx::quic::BM_MaxDataFrame_EncodeDecode);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif



