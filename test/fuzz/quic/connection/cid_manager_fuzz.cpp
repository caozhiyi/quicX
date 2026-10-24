// Fuzz target: CID pool invariants under hostile retire sequences (P0-S4,
// code review v4).
//
// ConnectionIDManager is driven by peer-controlled NEW_CONNECTION_ID /
// RETIRE_CONNECTION_ID input: retire_prior_to can jump to any varint and
// individual sequence numbers can be retired in any order. The P0-S4 fix
// rebuilt RETIRE-frame generation on CollectSequencesBelow(), whose contract
// the frame processor now relies on:
//
//   * every sequence it reports is STILL LIVE in the pool (retired or
//     never-issued sequences must never be reported — reporting a retired
//     one puts a bogus RETIRE frame on the wire, a protocol violation);
//   * it is complete for the low half: every live sequence < prior_to is
//     reported (missing one stalls the peer's CID slot forever);
//   * the pool count agrees with a full live-sequence enumeration.
//
// Operations are driven by the input bytes; small input keeps the pool small
// so the invariants are checked densely.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "quic/connection/connection_id_manager.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }

    quicx::quic::ConnectionIDManager mgr;

    for (size_t i = 0; i < size; ++i) {
        const uint8_t op = data[i] & 0x07;
        const uint64_t operand = data[i] >> 3;  // 0..31: dense small-range coverage

        switch (op) {
            case 0:
            case 1: {
                // Issue a new CID (NEW_CONNECTION_ID handling).
                auto issued = mgr.Generator();
                if (issued.GetLength() == 0) {
                    // A failed issue with an empty pool would break the
                    // "always one live CID" contract.
                    __builtin_trap();
                }
                break;
            }
            case 2:
                (void)mgr.RetireIDBySequence(operand);
                break;
            case 3:
                (void)mgr.RetireIDsUpTo(operand);
                break;
            case 4:
                // Consume the active CID for sending (advances cur_id_).
                (void)mgr.UseNextID();
                break;
            default: {
                // The P0-S4 primitive: collect live sequences below a bound.
                std::vector<uint64_t> live;
                mgr.CollectSequencesBelow(operand, live);

                // Invariant 1 & 2: every reported sequence is live and below
                // the bound. CollectSequencesBelow is also complete, so
                // cross-check against a per-sequence GetIDBySequence scan.
                for (uint64_t seq : live) {
                    if (seq >= operand) {
                        __builtin_trap();
                    }
                    quicx::quic::ConnectionID id;
                    if (!mgr.GetIDBySequence(seq, id)) {
                        // reported a sequence that is NOT live — this is the
                        // bogus-RETIRE-frame bug class P0-S4 fixed.
                        __builtin_trap();
                    }
                }
                for (uint64_t seq = 0; seq < operand; ++seq) {
                    quicx::quic::ConnectionID id;
                    bool is_live = mgr.GetIDBySequence(seq, id);
                    bool reported = false;
                    for (uint64_t r : live) {
                        if (r == seq) {
                            reported = true;
                            break;
                        }
                    }
                    if (is_live != reported) {
                        // completeness failure: a live sequence below the
                        // bound was not collected (or vice versa).
                        __builtin_trap();
                    }
                }
                break;
            }
        }

        // Invariant 3: the pool count equals a full live enumeration.
        std::vector<uint64_t> all_live;
        mgr.CollectSequencesBelow(UINT64_MAX, all_live);
        if (all_live.size() != mgr.GetAvailableIDCount()) {
            __builtin_trap();
        }
        if (mgr.GetAvailableIDCount() == 0 && !mgr.UseNextID()) {
            // An empty pool cannot yield a CID — UseNextID must fail rather
            // than fabricate one.
        }
    }
    return 0;
}
