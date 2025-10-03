#include <cstdint>
#include <cstddef>
#include <algorithm>

#include "quic/packet/packet_number.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    // Exercise truncated decode path
    if (size >= 3) {
        uint64_t largest_pn = static_cast<uint64_t>(data[0]);
        uint64_t truncated_bits = (static_cast<uint64_t>(data[1]) & 0x3) + 1; // 1..4
        uint64_t truncated_pn = static_cast<uint64_t>(data[2]) & ((1ULL << (truncated_bits * 8)) - 1);
        (void)quicx::quic::PacketNumber::Decode(largest_pn, truncated_pn, truncated_bits);
    }

    // Exercise byte encode/decode paths
    if (size >= 2) {
        uint32_t pn_len = (data[0] & 0x3) + 1; // 1..4
        pn_len = std::min<uint32_t>(pn_len, 4);
        if (size >= 1 + pn_len) {
            uint64_t pn = 0;
            // Decode from provided bytes
            uint8_t buf[8] = {0};
            for (uint32_t i = 0; i < pn_len; ++i) buf[i] = data[1 + i];
            (void)quicx::quic::PacketNumber::Decode(buf, pn_len, pn);

            // Re-encode to another buffer and decode again
            uint8_t out[8] = {0};
            (void)quicx::quic::PacketNumber::Encode(out, pn_len, pn);
            uint64_t pn2 = 0;
            (void)quicx::quic::PacketNumber::Decode(out, pn_len, pn2);
            (void)pn2;
        }
    }

    return 0;
}


