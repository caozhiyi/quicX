#if defined(QUICX_ENABLE_BENCHMARKS)
#include <benchmark/benchmark.h>
#include <memory>

#include "quic/frame/stream_frame.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/multi_block_buffer.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

namespace quicx {
namespace quic {

static std::shared_ptr<common::IBuffer> MakeBuf(size_t cap=64*1024) {
    auto pool = std::make_shared<common::BlockMemoryPool>(cap, /*add_num*/128);
    return std::make_shared<common::MultiBlockBuffer>(pool);
}

static void BM_StreamFrame_EncodeDecode(benchmark::State& state) {
   
    const size_t payload = static_cast<size_t>(state.range(0));
    std::vector<uint8_t> data_vec(payload, 0xEF);
    std::shared_ptr<common::StandaloneBufferChunk> data = std::make_shared<common::StandaloneBufferChunk>(payload);
    std::shared_ptr<common::SingleBlockBuffer> data_buf = std::make_shared<common::SingleBlockBuffer>(data);
    data_buf->Write(data_vec.data(), data_vec.size());


    for (auto _ : state) {
        auto buf = MakeBuf(payload + 128);
        StreamFrame f;
        f.SetOffset(0);
        f.SetData(data_buf->GetSharedReadableSpan());
        bool enc_ok = f.Encode(buf);
        benchmark::DoNotOptimize(enc_ok);
        StreamFrame g;
        bool dec_ok = g.Decode(buf);
        benchmark::DoNotOptimize(dec_ok);
    }
}
} // namespace quic
} // namespace quicx

BENCHMARK(quicx::quic::BM_StreamFrame_EncodeDecode)->Arg(64)->Arg(1024)->Arg(16*1024);
BENCHMARK_MAIN();
#else
int main() { return 0; }
#endif



