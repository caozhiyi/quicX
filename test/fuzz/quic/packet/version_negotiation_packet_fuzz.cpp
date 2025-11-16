#include <cstdint>
#include <cstddef>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"
#include "quic/packet/version_negotiation_packet.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    // Wrap input as a read buffer
    auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(size));
    in->Write(data, size);

    quicx::quic::VersionNegotiationPacket packet;
    if (!packet.DecodeWithoutCrypto(in, true)) {
        return 0;
    }

    // Encode decoded header back out
    auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(2048));
    if (!packet.Encode(out)) {
        return 0;
    }

    // Decode the re-encoded bytes again to exercise the decode path
    quicx::quic::VersionNegotiationPacket packet2;
    (void)packet2.DecodeWithoutCrypto(out, true);

    return 0;
}
