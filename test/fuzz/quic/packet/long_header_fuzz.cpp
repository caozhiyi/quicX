#include <cstdint>
#include <cstddef>

#include "quic/packet/header/long_header.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    // Wrap input as a read buffer
    auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(size));
    in->Write(data, size);

    quicx::quic::LongHeader header;
    if (!header.DecodeHeader(in, true)) {
        return 0;
    }

    // Encode decoded header back out
    auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(2048));
    if (!header.EncodeHeader(out)) {
        return 0;
    }

    // Decode the re-encoded bytes again to exercise the decode path
    quicx::quic::LongHeader header2;
    (void)header2.DecodeHeader(out, true);

    return 0;
}
