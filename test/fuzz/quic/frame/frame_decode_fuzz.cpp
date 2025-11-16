#include <cstdint>
#include <cstddef>
#include <vector>

#include "quic/frame/if_frame.h"
#include "quic/frame/frame_decode.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(size));
    in->Write(data, size);

    std::vector<std::shared_ptr<quicx::quic::IFrame>> frames;
    (void)quicx::quic::DecodeFrames(in, frames);

    // Optionally try to re-encode decoded packets to exercise encode path
    for (auto& frame : frames) {
        if (!frame) continue;
        uint8_t out_buf[2048];
        auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(sizeof(out_buf)));
        out->Write(out_buf, sizeof(out_buf));
        (void)(frame->Encode(out));
    }

    return 0;
}


