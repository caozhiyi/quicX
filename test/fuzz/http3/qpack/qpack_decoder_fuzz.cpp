// Fuzz target for the QPACK header-block decoder (#9, code review round 2).
//
// QpackEncoder::Decode() parses a peer-controlled header block: the prefix
// (Required Insert Count + Base), then a sequence of representations whose
// indexes / string lengths / dynamic-table references are all attacker
// controlled. The dynamic-table path (DecodeEncoderInstructions: Insert /
// SetCapacity / Duplicate) is fed from the same input bytes — it is the part
// where both prior review rounds found boundary bugs (eviction loop, index
// desync). This target only requires graceful failure: any return value is
// fine, crashes / hangs / OOM are not.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/qpack/qpack_encoder.h"

namespace {

std::shared_ptr<quicx::common::SingleBlockBuffer> WrapInput(const uint8_t* data, size_t size) {
    auto buf = std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(static_cast<uint32_t>(size)));
    buf->Write(data, static_cast<uint32_t>(size));
    return buf;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 2) {
        return 0;
    }

    // Variant A: decode the raw bytes as a header block with a dynamic table
    // available (capacity driven by the first input byte, covering the
    // disabled path at 0 and the default 1024 at mid-range).
    {
        quicx::http3::QpackEncoder dec;
        uint32_t cap = static_cast<uint32_t>(data[0]) * 64;
        dec.SetMaxTableCapacity(cap);
        dec.SetDynamicTableEnabled(true);
        std::vector<std::pair<std::string, std::string>> headers;
        (void)dec.Decode(WrapInput(data, size), headers);
    }

    // Variant B: feed the bytes to the encoder-instruction decoder FIRST
    // (Insert / SetCapacity / Duplicate — fills the dynamic table with
    // attacker-controlled entries), then decode the same bytes as a header
    // block. This reaches the dynamic-index and name-ref representation
    // paths that only fire when the table holds entries.
    {
        quicx::http3::QpackEncoder dec;
        dec.SetMaxTableCapacity(4096);
        dec.SetDynamicTableEnabled(true);
        (void)dec.DecodeEncoderInstructions(WrapInput(data, size));
        std::vector<std::pair<std::string, std::string>> headers;
        (void)dec.Decode(WrapInput(data, size), headers);
    }

    // Variant C: same as B but with a small capacity, so the instruction
    // stream drives eviction on every insert (the historically buggy loop).
    {
        quicx::http3::QpackEncoder dec;
        dec.SetMaxTableCapacity(64);
        dec.SetDynamicTableEnabled(true);
        (void)dec.DecodeEncoderInstructions(WrapInput(data, size));
        (void)dec.DecodeEncoderInstructions(WrapInput(data, size));
        std::vector<std::pair<std::string, std::string>> headers;
        (void)dec.Decode(WrapInput(data, size), headers);
    }

    return 0;
}
