#include <cstdint>
#include <cstddef>
#include <vector>

#include "common/buffer/buffer.h"
#include "quic/packet/if_packet.h"
#include "quic/packet/packet_decode.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    auto in = std::make_shared<quicx::common::Buffer>(
        const_cast<uint8_t*>(data), const_cast<uint8_t*>(data) + size);

    std::vector<std::shared_ptr<quicx::quic::IPacket>> packets;
    (void)quicx::quic::DecodePackets(in, packets);

    // Optionally try to re-encode decoded packets to exercise encode path
    for (auto& p : packets) {
        if (!p) continue;
        uint8_t out_buf[2048];
        auto out = std::make_shared<quicx::common::Buffer>(out_buf, out_buf + sizeof(out_buf));
        (void)(p->Encode(out));
    }

    return 0;
}


