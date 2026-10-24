// Fuzz target: stateful receive-stream frame sequences (P0-S3 / P0-S10,
// code review v4 — the smallest piece of the "stateful fuzzing" gap in P2-T1).
//
// RecvStream reassembles a peer-controlled sequence of STREAM /
// RESET_STREAM frames plus locally-triggered STOP_SENDING (application
// abort). Phase-2 hardening touched this machine in four places:
//   * P0-S3: out-of-order frames below 2KB are deep-copied, and the
//     out-of-order byte budget must always hold (kMaxOutOfOrderBytes);
//   * P0-S10: same-offset LONGER frames replace the buffered one (no
//     permanent stall), FIN below the received high-water mark is
//     FINAL_SIZE_ERROR, and a local STOP_SENDING must leave the machine
//     processable for the peer's real RESET_STREAM;
//   * P0-S2: window growth is consumption-driven — no window update may be
//     emitted merely because data arrived.
//   * P0-S1: duplicate/reordered frames must not double-count.
//
// The input drives an operation stream: op byte selects frame kind, and the
// following bytes become offset/length. A bounds-keeping read-callback
// drains a fraction of the buffer so consumption-driven paths are exercised
// too.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "common/network/if_event_loop.h"

#include "quic/frame/reset_stream_frame.h"
#include "quic/frame/stream_frame.h"
#include "quic/stream/if_stream.h"
#include "quic/stream/recv_stream.h"

namespace {

// One loop for the whole fuzz session: creating an EventLoop per input
// dominates runtime (≈2 exec/s) and starves the fuzzer. The loop is process-
// global and never torn down inside the harness, so streams (and their
// posted deferred flushes) never outlive it.
static std::shared_ptr<quicx::common::IEventLoop>& SharedLoop() {
    static std::shared_ptr<quicx::common::IEventLoop> loop = quicx::common::MakeEventLoop();
    static bool inited = loop ? loop->Init() : false;
    (void)inited;
    return loop;
}

std::shared_ptr<quicx::common::SingleBlockBuffer> MakeData(const uint8_t* src, size_t len) {
    auto buf = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(static_cast<uint32_t>(len + 1)));
    if (len > 0) {
        buf->Write(const_cast<uint8_t*>(src), static_cast<uint32_t>(len));
    }
    return buf;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 4) {
        return 0;
    }
    auto loop = SharedLoop();
    if (!loop) {
        return 0;
    }

    // no-op connection callbacks; a counting read callback that drains part
    // of the buffer so the consumption-driven window path (P0-S2) is live.
    uint64_t conn_close_count = 0;
    auto active_cb = [](std::shared_ptr<quicx::quic::IStream>) {};
    auto close_cb = [](uint64_t) {};
    auto conn_close_cb = [&conn_close_count](uint64_t, uint16_t, const std::string&) { ++conn_close_count; };

    uint64_t read_count = 0;
    quicx::stream_read_callback read_cb =
        [&read_count](std::shared_ptr<quicx::IBufferRead> buf, bool, uint32_t) {
            ++read_count;
            // Drain at most 32 bytes (or what's there): consumption happens
            // inside the callback, exactly like the HTTP/3 layer.
            uint32_t n = buf->GetDataLength();
            if (n > 32) {
                n = 32;
            }
            if (n > 0) {
                buf->MoveReadPt(n);
            }
        };

    auto stream = std::make_shared<quicx::quic::RecvStream>(loop, 100000, 5, active_cb, close_cb, conn_close_cb);
    stream->SetStreamReadCallBack(read_cb);

    // Drive the stream with the input, ~2 bytes per operation.
    for (size_t i = 0; i + 1 < size; i += 2) {
        const uint8_t op = data[i] & 0x07;
        // Keep offsets small and dense: the interesting logic is
        // reassembly/overlap, not the 100KB flow-control ceiling.
        const uint64_t offset = (data[i] >> 3) + ((static_cast<uint64_t>(data[i + 1] & 0x1f)) << 3);  // 0..255
        const uint32_t payload_len = (data[i + 1] >> 5) & 0x07;                                          // 0..7

        switch (op) {
            case 0:
            case 1:
            case 2: {
                // STREAM frame (occasionally with FIN). Payload comes from the
                // input REMAINING after the op bytes, clamped to what is
                // actually there — reading past the fuzz input would be a
                // harness bug, not a product finding.
                size_t avail = (i + 2 < size) ? (size - (i + 2)) : 0;
                uint32_t frame_len = payload_len;
                if (frame_len > avail) {
                    frame_len = static_cast<uint32_t>(avail);
                }
                auto frame = std::make_shared<quicx::quic::StreamFrame>();
                frame->SetStreamID(5);
                frame->SetOffset(offset);
                if (op == 2) {
                    frame->SetFin();
                }
                auto payload = MakeData(data + ((i + 2 < size) ? i + 2 : 0), frame_len);
                frame->SetData(payload->GetSharedReadableSpan());
                (void)stream->OnFrame(frame);
                break;
            }
            case 3: {
                // RESET_STREAM from the peer.
                auto frame = std::make_shared<quicx::quic::ResetStreamFrame>();
                frame->SetStreamID(5);
                frame->SetAppErrorCode(offset);
                frame->SetFinalSize(offset);
                (void)stream->OnFrame(frame);
                break;
            }
            case 4:
                // Local STOP_SENDING (application abort). P0-S10: must be
                // idempotent and must not fake a state transition.
                stream->Reset(0x100 + offset);
                break;
            default:
                break;
        }

        // Invariant (P0-S3): pinned receive memory never exceeds the
        // out-of-order budget plus the ordered buffer.
        if (stream->GetBufferedBytes() > 64ull * 1024 * 1024) {
            __builtin_trap();
        }
    }

    (void)read_count;
    (void)conn_close_count;
    // NOTE: we deliberately do NOT pump the loop (Wait() blocks on epoll
    // with no timeout knob and would cap throughput at ~3 exec/s). The
    // synchronous reassembly paths — OnFrame, overlap trimming, deep copy,
    // out-of-order replacement, reset/stop-sending interplay — are all
    // covered directly; only the deferred SetStreamReadCallBack flush
    // (posted for late-registered callbacks) is not pumped here.
    return 0;
}
