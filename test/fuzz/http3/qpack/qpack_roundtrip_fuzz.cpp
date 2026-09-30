// Fuzz target for the QPACK encode -> decode round trip (#9, code review
// round 2).
//
// Two independent QpackEncoder instances play the real connection roles
// (see IConnection: one encodes outbound headers, one decodes the peer's
// header blocks). The fuzzer drives:
//   * the negotiated dynamic-table capacity (including 0 = disabled),
//   * the header field set (data-derived names/values, bounded to keep the
//     input cheap),
// and bridges the two side channels the way the real streams do:
//   * encoder instructions (Insert)  : enc.instruction_sender_ -> a scratch
//     QpackEncoder::EncodeEncoderInstructions -> dec.DecodeEncoderInstructions,
//   * insert-count feedback          : delta of dec.GetInsertCount() ->
//     enc.OnPeerInsertCountIncrement (Known Received Count, RFC 9204 §2.1.4).
//
// Invariant: whenever both Encode and Decode succeed, the decoded field set
// must equal the input field set modulo ordering (Encode() re-orders
// pseudo-headers and sorts regular headers, so both sides are sorted before
// comparison).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "http3/qpack/qpack_encoder.h"

namespace {

using FieldList = std::vector<std::pair<std::string, std::string>>;

std::shared_ptr<quicx::common::SingleBlockBuffer> MakeBuf(uint32_t capacity) {
    return std::make_shared<quicx::common::SingleBlockBuffer>(
        std::make_shared<quicx::common::StandaloneBufferChunk>(capacity));
}

// Data-driven field list: consume the input as a sequence of length-prefixed
// (name, value) pairs. Total bytes are bounded so a tiny input cannot ask
// for a huge allocation (the lengths are capped by what remains).
FieldList ParseFields(const uint8_t*& data, size_t& size) {
    FieldList fields;
    while (size >= 2 && fields.size() < 32) {
        const uint8_t name_len = data[0];
        const uint8_t value_len = data[1];
        if (size < 2 + static_cast<size_t>(name_len) + value_len) {
            break;
        }
        fields.emplace_back(std::string(reinterpret_cast<const char*>(data + 2), name_len),
            std::string(reinterpret_cast<const char*>(data + 2 + name_len), value_len));
        data += 2 + name_len + value_len;
        size -= 2 + static_cast<size_t>(name_len) + value_len;
    }
    return fields;
}

FieldList Sorted(FieldList fields) {
    std::sort(fields.begin(), fields.end());
    return fields;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size < 4) {
        return 0;
    }

    // Byte 0..3 drive the negotiated capacity (0 included = dynamic table
    // disabled path). The rest is the field list.
    uint32_t cap = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                   (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
    cap %= (64 * 1024 + 1);
    data += 4;
    size -= 4;

    quicx::http3::QpackEncoder enc;
    quicx::http3::QpackEncoder dec;

    enc.SetLocalMaxTableCapacity(cap);
    enc.SetPeerMaxTableCapacity(cap);  // fuzz plays both SETTINGS sides
    enc.SetDynamicTableEnabled(cap > 0);
    dec.SetMaxTableCapacity(cap);
    dec.SetDynamicTableEnabled(cap > 0);

    // Bridge: encoder instructions -> decoder table, insert-count delta back.
    quicx::http3::QpackEncoder* enc_p = &enc;
    quicx::http3::QpackEncoder* dec_p = &dec;
    enc.SetInstructionSender([enc_p, dec_p](const FieldList& inserts) {
        // Build the instruction bytes with a scratch encoder the way the
        // production QpackEncoderSenderStream does, then feed them to the
        // decoder instance.
        quicx::http3::QpackEncoder scratch;
        auto instr = MakeBuf(static_cast<uint32_t>(64 + inserts.size() * (4 + 2 * 256)));
        if (!scratch.EncodeEncoderInstructions(inserts, instr)) {
            return;
        }
        uint64_t before = dec_p->GetInsertCount();
        if (dec_p->DecodeEncoderInstructions(instr)) {
            uint64_t delta = dec_p->GetInsertCount() - before;
            if (delta > 0) {
                enc_p->OnPeerInsertCountIncrement(delta);
            }
        }
    });

    FieldList headers = ParseFields(data, size);
    if (headers.empty()) {
        return 0;
    }

    auto wire = MakeBuf(static_cast<uint32_t>(64 + headers.size() * 600));
    if (!enc.Encode(headers, wire)) {
        return 0;  // graceful encode failure is fine
    }

    FieldList back;
    if (!dec.Decode(wire, back)) {
        return 0;  // graceful decode failure is fine (e.g. blocked / bad ref)
    }

    // Round-trip invariant: same multiset of (name, value) pairs.
    if (Sorted(back) != Sorted(headers)) {
        __builtin_trap();
    }
    return 0;
}
