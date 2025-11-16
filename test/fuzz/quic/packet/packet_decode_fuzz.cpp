#include <cstdint>
#include <cstddef>
#include <vector>

#include "quic/packet/if_packet.h"
#include "quic/packet/packet_decode.h"
#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"


extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    auto in = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(size));
    in->Write(data, size);

    std::vector<std::shared_ptr<quicx::quic::IPacket>> packets;
    (void)quicx::quic::DecodePackets(in, packets);

    // Optionally try to re-encode decoded packets to exercise encode path
    for (auto& p : packets) {
        if (!p) continue;
        auto out = std::make_shared<quicx::common::SingleBlockBuffer>(
            std::make_shared<quicx::common::StandaloneBufferChunk>(2048));
        (void)(p->Encode(out));
    }

    return 0;
}


