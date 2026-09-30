#ifndef QUIC_CONNECTION_SEND_POLICY
#define QUIC_CONNECTION_SEND_POLICY

#include <cstdint>

namespace quicx {
namespace quic {

// One iteration's send decision for the level-sticky burst loop
// (BaseConnection::TrySendNewBurst), computed once per packet by
// BaseConnection::ComputeSendPolicy() from the three build-time constraints —
// congestion window, RFC 9000 §8.1 anti-amplification budget and
// connection-level flow control — plus the narrow cwnd bypass rules
// (RFC 9002 §7 ACK-only / path-probing frames).
//
// Why a value struct: the pre-refactor loop threaded six interacting boolean
// locals (initial_padding / probe_bypass / ack_attached_this_burst /
// has_stream_data / fc_blocked_with_data / burst_has_challenge) and three
// independently-mutated size fields through 300 lines of loop body; every
// "which combination am I in?" question required re-deriving the combination
// from scratch. A policy snapshot makes each decision exactly once, in one
// place, with one name.
//
// The §8.1 entry is the load-bearing change. The budget used to be checked
// only AFTER a packet was built (DatagramEmitter's charge gate), so a
// budget-starved server either dropped whole built datagrams or deadlocked
// against a peer that had already discarded its Initial keys
// (quicx↔quinn handshakecorruption: "want:1069, remaining:591"). Here the
// budget participates in sizing BEFORE frames are gathered, so the loop
// emits a smaller packet instead of no packet.
struct SendPolicy {
    // ---- Sizing: all constraints, already intersected ----

    // Payload budget for this packet: min(cwnd, §8.1 budget), where the amp
    // term has already subtracted kAmpOverheadReserve — the emitter's gate
    // charges the FULL datagram (header + AEAD tag + payload), so a payload
    // sized exactly to the raw budget would still be refused there.
    uint32_t max_bytes{0};

    // §14.1 padding floor for the packet payload: normally
    // kMinInitialPacketSize for Initial packets, 0 otherwise, then clamped
    // down to the §8.1 payload budget when that cannot cover a padded
    // datagram. Padding a packet past its budget would only get it dropped
    // at the emitter — the exact failure this policy exists to prevent.
    uint32_t min_size{0};

    // Connection-level flow-control allowance for STREAM bytes; 0 = blocked.
    // CRYPTO frames are exempt and flow regardless (RFC 9000 §4.1), so a
    // blocked window never stalls the handshake.
    uint32_t max_stream_data_size{0};

    // ---- Content selection ----

    // May this datagram pull STREAM frames from the StreamManager?
    bool include_stream_data{false};

    // RFC 9002 §7 narrow bypass active: cwnd is exhausted but the packet may
    // still carry exempt frames (ACK / PATH_CHALLENGE / PING / PADDING).
    // Implies include_stream_data == false and min_size == 0 — a bypass
    // datagram must stay small enough to fit whatever budget remains.
    bool cwnd_bypass{false};

    // May this iteration attach the queued ACK frame? True at most once per
    // burst: the pending-ACK flag is consumed by the attach itself, and
    // later packets get fresh ACKs only when a new one becomes due.
    bool attach_ack{false};

    // Connection flow control exhausted while STREAM data is queued. Drives
    // SendManager::SetFlowControlBlocked() (the Bug #17 recheck timer).
    bool fc_blocked_with_stream_data{false};

    // ---- Termination ----

    enum class Stop : uint8_t {
        kNone,         // proceed with this packet
        kCwndBlocked,  // cwnd exhausted and no exempt frame pending
        kAmpStarved,   // §8.1 budget below kMinAmpUsableBytes
    };
    Stop stop{Stop::kNone};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_SEND_POLICY
