// Fuzz target for the QPACK integer / string-literal decoders.
//
// These read a peer's QPACK stream, so both length fields are attacker
// controlled. Two bugs lived here until this target existed -- there was no
// http3 fuzz coverage at all, only quic/packet and quic/frame:
//
//   * QpackDecodePrefixedInteger shifted by `m += 7` with no bound, so ten
//     continuation bytes reached `<< 70`: undefined behaviour, which is exactly
//     what the UBSan half of this build detects.
//
//   * QpackDecodeStringLiteral resized the output to a 64-bit wire length before
//     reading anything, so a five-byte input could request gigabytes.
//
// Both decoders are driven off the same input so a single corpus entry exercises
// the length parse and the body read together.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/qpack/util.h"

namespace {

std::shared_ptr<quicx::common::SingleBlockBuffer> WrapInput(const uint8_t* data, size_t size) {
    auto buf = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(static_cast<uint32_t>(size)));
    buf->Write(data, static_cast<uint32_t>(size));
    return buf;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    // Prefixed integer, across every legal prefix width. Width changes where the
    // continuation run starts, so each one is a distinct path.
    for (uint8_t prefix_bits = 1; prefix_bits <= 8; ++prefix_bits) {
        auto buf = WrapInput(data, size);
        uint8_t first_byte = 0;
        uint64_t value = 0;
        (void)quicx::http3::QpackDecodePrefixedInteger(buf, prefix_bits, first_byte, value);
    }

    // String literal:7-bit length prefix, Huffman flag in the MSB. Covers both
    // the raw and the Huffman-decode branch depending on that bit.
    {
        auto buf = WrapInput(data, size);
        std::string out;
        (void)quicx::http3::QpackDecodeStringLiteral(buf, out);
    }

    // Same input with the Huffman bit forced on, so a corpus that happens to
    // hold only raw literals still reaches the Huffman path.
    if (size >= 1) {
        auto buf = WrapInput(data, size);
        auto span = buf->GetReadableSpan();
        if (span.GetLength() > 0) {
            span.GetStart()[0] |= 0x80;
        }
        std::string out;
        (void)quicx::http3::QpackDecodeStringLiteral(buf, out);
    }

    // Round-trip: whatever the decoder accepted must re-encode and decode back
    // to the same bytes. Catches encoder/decoder disagreements -- the Huffman
    // flag used to be set while the body was written uncompressed.
    {
        auto buf = WrapInput(data, size);
        std::string decoded;
        if (quicx::http3::QpackDecodeStringLiteral(buf, decoded)) {
            auto reencoded = std::make_shared<quicx::common::SingleBlockBuffer>(
                std::make_shared<quicx::common::StandaloneBufferChunk>(static_cast<uint32_t>(decoded.size() + 16)));
            if (quicx::http3::QpackEncodeStringLiteral(decoded, reencoded, /*huffman=*/false)) {
                std::string again;
                if (quicx::http3::QpackDecodeStringLiteral(reencoded, again) && again != decoded) {
                    __builtin_trap();
                }
            }
        }
    }

    return 0;
}
