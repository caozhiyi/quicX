// Fuzz target: HTTP/3 frame decoder (P2-T1 / P0-S6 / P1-H3, code review v4).
//
// FrameDecoder parses a peer-controlled byte stream into H3 frames. The
// phase-1/2 hardening that lives on this path:
//   * P1-H3: SETTINGS with duplicate ids, reserved ids 0x02-0x05, or 64-bit
//     GREASE-style ids must be rejected, not silently truncated;
//   * P0-S6: HEADERS / PUSH_PROMISE frames with an announced length above
//     kMaxHeadersFrameLength (256KB) must be rejected immediately instead of
//     parking the stream in kNeedMoreData while the buffer grows;
//   * P2 hardening: DATA frames with a truncated / oversize length must
//     fail closed, and unknown frame types must be skipped by their
//     declared length without desynchronising the stream position.
//
// The bytes are fed incrementally in random-sized chunks so the partial-
// frame resume path (kNeedMoreData) is exercised, not just whole-frame
// decodes.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/frame/frame_decoder.h"
#include "http3/frame/if_frame.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    // Whole-buffer decode: one call.
    {
        quicx::http3::FrameDecoder decoder;
        auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(static_cast<uint32_t>(size)));
        in->Write(const_cast<uint8_t*>(data), static_cast<uint32_t>(size));
        std::vector<std::shared_ptr<quicx::http3::IFrame>> frames;
        (void)decoder.DecodeFrames(in, frames);

        // Any produced frame must be re-encodable without crashes; Settings
        // and Headers both expose Encode.
        for (auto& f : frames) {
            if (!f) continue;
            auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
                std::make_shared<quicx::common::StandaloneBufferChunk>(4096));
            (void)f->Encode(out);
        }
    }

    // Incremental decode: the same bytes in pseudo-random 1..16-byte chunks,
    // resuming partial frames across calls. The chunk sizes come from the
    // input itself so the corpus controls the split points.
    {
        quicx::http3::FrameDecoder decoder;
        size_t pos = 0;
        size_t seed = 0;
        std::vector<std::shared_ptr<quicx::http3::IFrame>> frames;
        while (pos < size) {
            seed = (seed * 31 + data[pos]) & 0x0f;  // 0..15
            size_t chunk = seed + 1;
            if (pos + chunk > size) {
                chunk = size - pos;
            }
            auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
                std::make_shared<quicx::common::StandaloneBufferChunk>(static_cast<uint32_t>(chunk)));
            in->Write(const_cast<uint8_t*>(data + pos), static_cast<uint32_t>(chunk));
            (void)decoder.DecodeFrames(in, frames);
            pos += chunk;
        }
    }
    return 0;
}
