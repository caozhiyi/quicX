// =============================================================================
// frame_perf_test.cpp - Full frame-type encode / decode benchmark matrix
// =============================================================================
//
// cpu_hotspot_test only covers AckFrame + StreamFrame.  This suite extends
// the matrix to every frame type implemented in src/quic/frame/ so we can
// catch regressions in less-common frames (loss recovery and path validation
// are dominated by them).
//
// It also contains a dedicated "AckFrame with many gaps" benchmark --- the
// real-world hot case when there is heavy reordering or loss.
//
// =============================================================================

#if defined(QUICX_ENABLE_BENCHMARKS)

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/frame/ack_frame.h"
#include "quic/frame/connection_close_frame.h"
#include "quic/frame/crypto_frame.h"
#include "quic/frame/frame_decode.h"
#include "quic/frame/handshake_done_frame.h"
#include "quic/frame/max_stream_data_frame.h"
#include "quic/frame/new_connection_id_frame.h"
#include "quic/frame/new_token_frame.h"
#include "quic/frame/path_challenge_frame.h"
#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stop_sending_frame.h"
#include "quic/frame/stream_frame.h"

namespace quicx {
namespace perf {

static constexpr size_t kBufCap = 4096;

static std::shared_ptr<common::SingleBlockBuffer> MakeBuffer(size_t cap = kBufCap) {
    return std::make_shared<common::SingleBlockBuffer>(
        std::make_shared<common::StandaloneBufferChunk>(cap));
}

// Helper: encode a frame, produce a raw byte vector that can be re-fed into
// DecodeFrames repeatedly.
template <typename FrameT>
static std::vector<uint8_t> EncodeOneFrame(const std::shared_ptr<FrameT>& frame) {
    auto buf = MakeBuffer();
    frame->Encode(buf);
    auto span = buf->GetReadableSpan();
    return std::vector<uint8_t>(span.GetStart(), span.GetStart() + span.GetLength());
}

// Generic encode benchmark template.
template <typename FrameT, typename Factory>
static void BM_FrameEncode(benchmark::State& state, Factory factory) {
    for (auto _ : state) {
        auto frame = factory();
        auto buf = MakeBuffer();
        bool ok = frame->Encode(buf);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(buf->GetDataLength());
    }
    state.SetItemsProcessed(state.iterations());
}

// Generic decode benchmark: pre-encodes the frame once, then decodes from a
// fresh buffer each iteration.
template <typename FrameT, typename Factory>
static void BM_FrameDecode(benchmark::State& state, Factory factory) {
    auto frame = factory();
    auto bytes = EncodeOneFrame(frame);

    for (auto _ : state) {
        auto buf = MakeBuffer(bytes.size() + 16);
        buf->Write(bytes.data(), static_cast<uint32_t>(bytes.size()));

        std::vector<std::shared_ptr<quic::IFrame>> frames;
        bool ok = quic::DecodeFrames(buf, frames);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(frames.size());
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["wire_B"] = static_cast<double>(bytes.size());
}

// ===========================================================================
// Factories for every frame type (one function per type to stay type-safe)
// ===========================================================================

static std::shared_ptr<quic::CryptoFrame> MakeCryptoFrame() {
    auto f = std::make_shared<quic::CryptoFrame>();
    f->SetOffset(1024);
    f->SetEncryptionLevel(1);
    auto data_buf = MakeBuffer(256);
    uint8_t data[128] = {0};
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = static_cast<uint8_t>(i);
    data_buf->Write(data, sizeof(data));
    f->SetData(data_buf->GetSharedReadableSpan());
    return f;
}

static std::shared_ptr<quic::ResetStreamFrame> MakeResetStreamFrame() {
    auto f = std::make_shared<quic::ResetStreamFrame>();
    f->SetStreamID(4);
    f->SetAppErrorCode(0x10);
    f->SetFinalSize(12345);
    return f;
}

static std::shared_ptr<quic::StopSendingFrame> MakeStopSendingFrame() {
    auto f = std::make_shared<quic::StopSendingFrame>();
    f->SetStreamID(4);
    f->SetAppErrorCode(0x20);
    return f;
}

static std::shared_ptr<quic::MaxStreamDataFrame> MakeMaxStreamDataFrame() {
    auto f = std::make_shared<quic::MaxStreamDataFrame>();
    f->SetStreamID(4);
    f->SetMaximumData(1ull * 1024 * 1024);
    return f;
}

static std::shared_ptr<quic::NewConnectionIDFrame> MakeNewConnectionIdFrame() {
    auto f = std::make_shared<quic::NewConnectionIDFrame>();
    f->SetSequenceNumber(3);
    f->SetRetirePriorTo(0);
    uint8_t cid[8] = {0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8};
    f->SetConnectionID(cid, sizeof(cid));
    uint8_t token[16] = {0};
    for (size_t i = 0; i < sizeof(token); ++i) token[i] = static_cast<uint8_t>(i);
    f->SetStatelessResetToken(token);
    return f;
}

static std::shared_ptr<quic::PathChallengeFrame> MakePathChallengeFrame() {
    auto f = std::make_shared<quic::PathChallengeFrame>();
    f->MakeData();
    return f;
}

static std::shared_ptr<quic::ConnectionCloseFrame> MakeConnectionCloseFrame() {
    auto f = std::make_shared<quic::ConnectionCloseFrame>();
    f->SetErrorCode(0x100);
    f->SetErrFrameType(0);
    f->SetReason("benchmark close");
    return f;
}

static uint8_t g_token_storage[64] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
};

static std::shared_ptr<quic::NewTokenFrame> MakeNewTokenFrame() {
    auto f = std::make_shared<quic::NewTokenFrame>();
    f->SetToken(g_token_storage, 16);
    return f;
}

static std::shared_ptr<quic::HandshakeDoneFrame> MakeHandshakeDoneFrame() {
    return std::make_shared<quic::HandshakeDoneFrame>();
}

// ===========================================================================
// Scenario: AckFrame with many ranges (loss-recovery hot case)
// ===========================================================================

static void BM_Frame_AckFrame_ManyRanges_Encode(benchmark::State& state) {
    const int n_ranges = static_cast<int>(state.range(0));

    auto make_ack = [&]() {
        auto ack = std::make_shared<quic::AckFrame>();
        ack->SetLargestAck(1'000'000);
        ack->SetAckDelay(50);
        ack->SetFirstAckRange(5);
        for (int i = 0; i < n_ranges; ++i) {
            ack->AddAckRange(/*gap=*/1 + (i % 4), /*range=*/2 + (i % 6));
        }
        return ack;
    };

    for (auto _ : state) {
        auto ack = make_ack();
        auto buf = MakeBuffer();
        bool ok = ack->Encode(buf);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(buf->GetDataLength());
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["ranges"] = static_cast<double>(n_ranges);
}

static void BM_Frame_AckFrame_ManyRanges_Decode(benchmark::State& state) {
    const int n_ranges = static_cast<int>(state.range(0));

    auto ack = std::make_shared<quic::AckFrame>();
    ack->SetLargestAck(1'000'000);
    ack->SetAckDelay(50);
    ack->SetFirstAckRange(5);
    for (int i = 0; i < n_ranges; ++i) {
        ack->AddAckRange(1 + (i % 4), 2 + (i % 6));
    }
    auto bytes = EncodeOneFrame(ack);

    for (auto _ : state) {
        auto buf = MakeBuffer(bytes.size() + 32);
        buf->Write(bytes.data(), static_cast<uint32_t>(bytes.size()));

        std::vector<std::shared_ptr<quic::IFrame>> frames;
        bool ok = quic::DecodeFrames(buf, frames);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(frames.size());
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["ranges"] = static_cast<double>(n_ranges);
    state.counters["wire_B"] = static_cast<double>(bytes.size());
}

// ===========================================================================
// Per-frame encode benches
// ===========================================================================

static void BM_Frame_Encode_Crypto(benchmark::State& s) {
    BM_FrameEncode<quic::CryptoFrame>(s, &MakeCryptoFrame);
}
static void BM_Frame_Encode_ResetStream(benchmark::State& s) {
    BM_FrameEncode<quic::ResetStreamFrame>(s, &MakeResetStreamFrame);
}
static void BM_Frame_Encode_StopSending(benchmark::State& s) {
    BM_FrameEncode<quic::StopSendingFrame>(s, &MakeStopSendingFrame);
}
static void BM_Frame_Encode_MaxStreamData(benchmark::State& s) {
    BM_FrameEncode<quic::MaxStreamDataFrame>(s, &MakeMaxStreamDataFrame);
}
static void BM_Frame_Encode_NewConnectionId(benchmark::State& s) {
    BM_FrameEncode<quic::NewConnectionIDFrame>(s, &MakeNewConnectionIdFrame);
}
static void BM_Frame_Encode_PathChallenge(benchmark::State& s) {
    BM_FrameEncode<quic::PathChallengeFrame>(s, &MakePathChallengeFrame);
}
static void BM_Frame_Encode_ConnectionClose(benchmark::State& s) {
    BM_FrameEncode<quic::ConnectionCloseFrame>(s, &MakeConnectionCloseFrame);
}
static void BM_Frame_Encode_NewToken(benchmark::State& s) {
    BM_FrameEncode<quic::NewTokenFrame>(s, &MakeNewTokenFrame);
}
static void BM_Frame_Encode_HandshakeDone(benchmark::State& s) {
    BM_FrameEncode<quic::HandshakeDoneFrame>(s, &MakeHandshakeDoneFrame);
}

// ===========================================================================
// Per-frame decode benches
// ===========================================================================

static void BM_Frame_Decode_Crypto(benchmark::State& s) {
    BM_FrameDecode<quic::CryptoFrame>(s, &MakeCryptoFrame);
}
static void BM_Frame_Decode_ResetStream(benchmark::State& s) {
    BM_FrameDecode<quic::ResetStreamFrame>(s, &MakeResetStreamFrame);
}
static void BM_Frame_Decode_StopSending(benchmark::State& s) {
    BM_FrameDecode<quic::StopSendingFrame>(s, &MakeStopSendingFrame);
}
static void BM_Frame_Decode_MaxStreamData(benchmark::State& s) {
    BM_FrameDecode<quic::MaxStreamDataFrame>(s, &MakeMaxStreamDataFrame);
}
static void BM_Frame_Decode_NewConnectionId(benchmark::State& s) {
    BM_FrameDecode<quic::NewConnectionIDFrame>(s, &MakeNewConnectionIdFrame);
}
static void BM_Frame_Decode_PathChallenge(benchmark::State& s) {
    BM_FrameDecode<quic::PathChallengeFrame>(s, &MakePathChallengeFrame);
}
static void BM_Frame_Decode_ConnectionClose(benchmark::State& s) {
    BM_FrameDecode<quic::ConnectionCloseFrame>(s, &MakeConnectionCloseFrame);
}
static void BM_Frame_Decode_NewToken(benchmark::State& s) {
    BM_FrameDecode<quic::NewTokenFrame>(s, &MakeNewTokenFrame);
}
static void BM_Frame_Decode_HandshakeDone(benchmark::State& s) {
    BM_FrameDecode<quic::HandshakeDoneFrame>(s, &MakeHandshakeDoneFrame);
}

}  // namespace perf
}  // namespace quicx

// ===========================================================================
// Registration
// ===========================================================================

BENCHMARK(quicx::perf::BM_Frame_Encode_Crypto)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_Crypto)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Encode_ResetStream)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_ResetStream)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Encode_StopSending)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_StopSending)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Encode_MaxStreamData)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_MaxStreamData)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Encode_NewConnectionId)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_NewConnectionId)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Encode_PathChallenge)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_PathChallenge)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Encode_ConnectionClose)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_ConnectionClose)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Encode_NewToken)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_NewToken)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Encode_HandshakeDone)->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_Decode_HandshakeDone)->Unit(benchmark::kNanosecond);

// Ack frame with many ranges (real-world hot case on loss-heavy links).
BENCHMARK(quicx::perf::BM_Frame_AckFrame_ManyRanges_Encode)
    ->Arg(1)->Arg(16)->Arg(64)->Arg(128)
    ->Unit(benchmark::kNanosecond);
BENCHMARK(quicx::perf::BM_Frame_AckFrame_ManyRanges_Decode)
    ->Arg(1)->Arg(16)->Arg(64)->Arg(128)
    ->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();

#else
int main() { return 0; }
#endif
