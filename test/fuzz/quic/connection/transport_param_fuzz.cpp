// Fuzz target: QUIC transport parameters (P2-T1, code review v4).
//
// TransportParam::Decode parses a peer-controlled blob: every parameter id
// is a varint and every value is length-prefixed attacker bytes. Phase-2
// hardening touched this surface twice —
//   * the per-direction split (P0-C5/P0-S4): Merge() must keep OUR
//     active_connection_id_limit_ / ack params and only record the peer's,
//     so a hostile blob must not be able to overwrite our declarations;
//   * the 0-RTT early-data context (P0-S9): EvaluateEarlyDataContext()
//     walks the decoded fields and must stay deterministic and in-bounds
//     for any accepted blob.
//
// Invariants asserted (a violation means memory corruption or a broken
// split, not just a malformed input):
//   * the context bytes are identical across two evaluations;
//   * after Merge, our active_connection_id_limit_ is unchanged.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "common/buffer/buffer_span.h"

#include "quic/connection/transport_param.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > 4096) {
        return 0;
    }

    for (bool received_by_server : {false, true}) {
        quicx::quic::TransportParam tp;
        quicx::common::BufferSpan span(const_cast<uint8_t*>(data), static_cast<uint32_t>(size));
        if (!tp.Decode(span, received_by_server)) {
            continue;  // malformed blob: fine, only crashes matter
        }

        // P0-S9: context evaluation must be deterministic for a given state.
        auto ctx1 = tp.EvaluateEarlyDataContext();
        auto ctx2 = tp.EvaluateEarlyDataContext();
        if (ctx1 != ctx2) {
            // Deterministic fields cannot disagree; abort so the fuzzer
            // keeps this input.
            __builtin_trap();
        }

        // P0-S4 split: merging a decoded (peer) blob into OUR transport
        // parameters must never overwrite our per-direction declarations.
        quicx::quic::TransportParam ours;
        const uint64_t our_cid_limit = ours.GetActiveConnectionIdLimit();
        const uint64_t our_max_ack_delay = ours.GetMaxAckDelay();
        const uint64_t our_ack_exponent = ours.GetackDelayExponent();
        ours.Merge(tp);
        if (ours.GetActiveConnectionIdLimit() != our_cid_limit ||
            ours.GetMaxAckDelay() != our_max_ack_delay ||
            ours.GetackDelayExponent() != our_ack_exponent) {
            // A hostile blob rewrote OUR advertised parameters — the exact
            // cross-direction confusion P0-C5/P0-S4 fixed.
            __builtin_trap();
        }

        // Round-trip the merged params through Encode where possible: any
        // accepted state must be encodable without buffer overrun. Use a
        // heap buffer sized like the production caller (1024) to also catch
        // oversize states via the encoder's own bounds check.
        std::vector<uint8_t> out(1024);
        quicx::common::BufferSpan out_span(out.data(), static_cast<uint32_t>(out.size()));
        size_t written = 0;
        (void)ours.Encode(out_span, written);
    }
    return 0;
}
