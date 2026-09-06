// Copyright (c) 2024 The quicX Authors. All rights reserved.
// QUIC layer centralized configuration constants

#ifndef QUIC_CONFIG_H
#define QUIC_CONFIG_H

#include <cstddef>
#include <cstdint>

namespace quicx {
namespace quic {

// ============================================================================
// Packet Layer Configuration
// ============================================================================

// Default destination Connection ID length for short header packets (RFC 9000 Section 17.3)
// 20 bytes is the maximum CID length, used when encoding short headers
static constexpr uint32_t kDefaultDestinationCidLength = 20;

// Maximum payload size for a single frame (bytes)
// 1420 bytes fits within MTU 1500 after QUIC header + AEAD tag + IP/UDP headers
static constexpr uint32_t kMaxFramePayload = 1420;

// Maximum length (bytes) of a CONNECTION_CLOSE reason phrase. RFC 9000 leaves
// this unbounded; 16KB is a safe ceiling against memory exhaustion.
static constexpr uint32_t kMaxConnectionCloseReasonLength = 16384;

// Maximum NEW_CONNECTION_ID sequence numbers retired per single frame. Caps the
// retirement loop to avoid DoS from large retire_prior_to values.
static constexpr uint64_t kMaxRetireConnectionIdPerFrame = 256;

// ============================================================================
// Connection-level Flow Control
// ============================================================================

// Bytes remaining before sending DATA_BLOCKED to peer (RFC 9000 Section 4.1)
// 16KB (~11 MTU packets) provides sufficient advance warning
// Optimized from original 8912 to align with standard buffer sizes
static constexpr uint64_t kDataBlockedThreshold = 16384;

// Streams remaining before sending STREAMS_BLOCKED to peer
static constexpr uint64_t kStreamsBlockedThreshold = 4;

// Bytes remaining before sending MAX_DATA to increase peer's send limit
static constexpr uint64_t kDataIncreaseThreshold = 512 * 1024;  // 512KB

// Amount to increase connection-level data limit in MAX_DATA frame (RFC 9000 Section 4.2)
// 2MB provides ample headroom for high-throughput transfers
static constexpr uint64_t kDataIncreaseAmount = 2 * 1024 * 1024;  // 2MB

// Streams remaining before proactively sending MAX_STREAMS
static constexpr uint64_t kStreamsIncreaseThreshold = 4;

// Amount to increase MAX_STREAMS limit per frame
static constexpr uint64_t kStreamsIncreaseAmount = 10;

// Maximum ACK delay in milliseconds (RFC 9000 Section 18.2, default 25ms)
// Balances ACK frequency with protocol overhead
static constexpr uint32_t kMaxAckDelay = 25;

// Fallback re-check interval (ms) for connection-level flow control. When a
// connection is held back purely by the peer's MAX_DATA, a low-frequency timer
// re-examines it so it is not dropped from the worker's active set until idle
// timeout (see Bug #17).
static constexpr uint32_t kFlowControlRecheckIntervalMs = 100;

// ============================================================================
// Stream-level Flow Control
// ============================================================================

// Bytes remaining before sending STREAM_DATA_BLOCKED to peer
// 4KB (~3 MTU packets) provides reasonable buffer before blocking
// Optimized from original 2048 for high-throughput scenarios
static constexpr uint64_t kStreamDataBlockedThreshold = 4096;

// Amount to increase stream-level receive window in MAX_STREAM_DATA frame
// 2MB aligns with connection-level window increment
static constexpr uint64_t kStreamWindowIncrement = 2 * 1024 * 1024;  // 2MB

// Amount to increase stream window when peer sends STREAM_DATA_BLOCKED
// 4MB provides burst capacity for large file transfers
static constexpr uint64_t kBlockedWindowIncrement = 4 * 1024 * 1024;  // 4MB

// Maximum stream-level receive window size (hard upper limit)
// Prevents malicious peers from inflating window indefinitely via BLOCKED frames
static constexpr uint64_t kMaxStreamWindowSize = 64 * 1024 * 1024;  // 64MB

// Maximum number of bytes of out-of-order stream data buffered per stream.
// Bounds memory from a (potentially malicious) peer. QUIC streams tolerate
// out-of-order delivery, so this is a *memory* limit only — when exceeded the
// oldest buffered frame is evicted (NOT a connection close). Sized far above
// any legitimate single-stream transfer so normal large-file transfers that
// momentarily stall behind a single lost gap are never affected. Replaces the
// old kMaxOutOfOrderFrames=1024 frame-count limit, whose CONNECTION_CLOSE on
// overflow killed legitimate transfers (P0: quicx self-loop transfer/chacha20/
// rebind-port/rebind-addr/connectionmigration).
static constexpr uint64_t kMaxOutOfOrderBytes = 32 * 1024 * 1024;  // 32MB

// ============================================================================
// Congestion Control Configuration
// ============================================================================

// Congestion control algorithm selection.
// Available options:
//   "reno"   - NewReno (default, conservative, good baseline)
//   "cubic"  - CUBIC (Linux default, better throughput on high-BDP paths)
//   "bbrv1"  - BBR v1 (Google's bandwidth-based CC)
//   "bbrv2"  - BBR v2 (improved fairness and loss handling)
//   "bbrv3"  - BBR v3 (latest, improved ProbeRTT and loss tolerance)
//
// Change this value and rebuild to quickly switch CC algorithm for benchmarking.
// No environment variable needed — just modify, make, and run.
static constexpr const char* kDefaultCongestionControl = "cubic";

// ============================================================================
// TLS/Crypto Configuration
// ============================================================================

// Legacy default for TLS peer certificate verification. Currently unused:
// verification is controlled per-client by QuicClientConfig::verify_peer_
// (include/quicx/quic/if_quic_client.h, default true), which is passed
// through to TLSCtxClient::Init().
static constexpr bool kDefaultTlsVerifyPeer = false;

// ============================================================================
// Server Configuration
// ============================================================================

// Handshake timeout in milliseconds
// 30 seconds, aligned with the client-side handshake timeout (30 s).
// Under hostile-network testcases (30% burst loss/corruption) the server's
// handshake flight can be wiped several times in a row; the PTO backoff
// (0.75s/1.5s/3s/6s/12s...) needs far more than 5 s of attempts to push the
// flight through. With a 5 s watchdog the server aborted while the client
// was still waiting, then every client PING probe Initial (no ClientHello)
// spawned a phantom server connection that could only ACK and hit the same
// 5 s timeout — a ~6 s death loop until the client's 30 s timeout.
static constexpr uint32_t kHandshakeTimeoutMs = 30000;

// Connection-migration dispatch timeout in milliseconds.
// When a migration API (InitiateMigration / InitiateMigrationTo) is invoked from
// a thread other than the connection's event-loop thread, the work is posted to
// the loop and the caller blocks on a future for this long before giving up with
// kFailedTimeout. If the loop cannot drain a single posted task within 5 s it is
// effectively stalled/dead, so failing fast is the correct outcome.
static constexpr uint32_t kMigrationDispatchTimeoutMs = 5000;

// Retry token Connection ID length (RFC 9000 Section 8.1)
// 8 bytes is the minimum recommended CID length for retry tokens
static constexpr uint32_t kRetryCidLength = 8;

// Packet pool size (number of pre-allocated packet buffers)
// 256 (power of 2) is optimized for memory pool management and high concurrency
// Increased from original 200 to reduce allocation overhead under load
static constexpr uint32_t kPacketPoolSize = 256;

// Individual packet buffer size (bytes)
// 1500 bytes matches typical Ethernet MTU
static constexpr uint32_t kPacketBufferSize = 1500;

// Number of blocks in packet pool allocator
// 64 blocks balances memory overhead with allocation efficiency
static constexpr uint32_t kPacketPoolBlockCount = 64;

// ============================================================================
// Key Update Configuration
// ============================================================================

// Default thresholds that trigger a KeyUpdate (RFC 9001 Section 6). These are
// the compile-time defaults; a connection may tighten them at runtime.
static constexpr uint64_t kKeyUpdateBytesThreshold = 512 * 1024;   // 512KB of data sent
static constexpr uint64_t kKeyUpdatePacketNumberThreshold = 1000;  // packets sent

// ============================================================================
// UDP Receive Configuration
// ============================================================================

// Socket-drain batch ceiling for UdpReceiver::OnRead.
// Trades latency-per-wakeup against ACK aggregation:
//   - Too small  → ACK aggregation defeated (wait_ack_packet_numbers_ never
//                  accumulates beyond 1 entry, kAckThreshold branch is rarely
//                  hit, ACKs flow ~1:1 with data packets).
//   - Too large  → starves co-resident timers/write events that share this
//                  loop iteration.
// 64 mirrors the send-side kMaxPacketsPerRound and is comfortably above 1 BDP
// at typical cwnd. Must be in [1, 256] (256 is common::kRecvBatchHardCap in
// common/config.h, enforced inside RecvFromBatch and mirrored by the stack
// array size in OnRead).
static constexpr int kMaxRecvBatch = 64;

// Per-connection drain cap inside Worker::ProcessSend (one drain round).
// Tuning history (200 MB loopback file_transfer, macOS arm64) — post
// recv-batching (recvmmsg/drain up to kMaxRecvBatch per wakeup):
//     64   -> ~34.0 MB/s (old default)
//    128   -> ~36.6 MB/s (chosen — +7.6%, ack feedback keeps up)
//    256   -> ~30.4 MB/s (-10.6%; ack-feedback re-starves)
//   1024   -> ~31.5 MB/s (-7.4%)
// 128 is the sweet spot now that recv-side drains in batches: ack feedback
// can match a doubled send window per round, but 256+ pushes ack processing
// past its budget per loop iteration. Legal range [1, 1024]; the thread-local
// tx_batch reserves this many slots so changing the value affects steady-
// state memory by ~16B per slot.
static constexpr int kMaxPacketsPerRound = 128;

// Number of ack-eliciting packets that must accumulate before
// RecvControl::ShouldSendImmediateAck flushes an ACK. RFC 9000 §13.2.2 only
// requires ACKing "at least every 2 ack-eliciting packets" as a *lower bound*
// against unbounded delay, not an upper bound. Flushing at exactly 2 defeats
// ACK aggregation (~1:1 ACK ratio) and on fast paths chains cwnd growth to
// per-packet RTT, starving throughput. A larger threshold lets us keep
// aggregating when packets arrive faster than max_ack_delay; the
// max_ack_delay_ timer still bounds worst-case ACK latency for sparse
// traffic. Legal range [1, 1024].
static constexpr size_t kAckThreshold = 10;

// Maximum ACK ranges packed into a single ACK frame (fits within MTU).
static constexpr uint32_t kMaxAckRanges = 64;

// Maximum segments per UDP GSO sendmsg(2) call, bounded by the kernel's
// UDP_MAX_SEGMENTS ceiling. Also sizes the per-thread GSO scratch buffer.
static constexpr size_t kGsoMaxSegments = 64;

// ============================================================================
// RTT / PTO Configuration (RFC 9002)
// ============================================================================

// Built-in default initial RTT (milliseconds).
//
// RFC 9002 §6.2.2 recommends 333 ms; we use 250 ms which is aggressive-but-safe
// for typical Internet RTTs and keeps our initial PTO
// (= SRTT + 4·RTTVAR + max_ack_delay = 250 + 500 + 25 = 775 ms) within the
// same order of magnitude as the RFC baseline (~1.1 s).
//
// Do **not** hard-code this value at callers — always go through
// GetDefaultInitialRtt(), which honours the process-level override installed
// by SetDefaultInitialRtt(). This is the P3 knob from
// docs/internal/perf_e2e_analysis.md §6.
static constexpr uint32_t kInitRttDefaultMs = 250;

// Max PTO backoff exponent once the handshake is confirmed: 2^6 = 64x.
// Keeps a long-lived, genuinely dead connection from probing forever.
static constexpr uint32_t kMaxPTOBackoff = 6;

// Backoff cap while the handshake is unconfirmed (RFC 9002 §6.2.2.1).
//
// The 2^6 cap above is the wrong shape for the unconfirmed handshake, which
// is the one phase RFC 9002 asks us to probe aggressively: nothing has been
// acknowledged yet, so every doubling is another doubling of the time the
// peer sits on a half-open connection.
//
// Concretely, with a base PTO around one RTT (~235 ms on the interop path)
// the uncapped sequence reaches 3.8 s at 2^4, 7.5 s at 2^5 and 15 s at
// 2^6 -- i.e. the last rounds are spaced wider than the connection's own
// idle timeout, so the retransmissions that could still have got through
// never happen inside the window.
//
// Capping at 2^2 while unconfirmed keeps the probes at 1x/2x/4x of the
// base PTO and then holds there, which is what turns "a few sparse
// retransmissions" into a density that can actually punch through a lossy
// path. The confirmed cap is unchanged: once the peer is ACKing us, the
// old long tail costs nothing.
static constexpr uint32_t kMaxPTOBackoffUnconfirmed = 2;

// Hard stop on consecutive PTOs without an ACK (~3 PTO cycles for idle
// timeout).
static constexpr uint32_t kMaxConsecutivePTOs = 16;

// ============================================================================
// Loss Detection (RFC 9002 Section 6.1.1)
// ============================================================================

// Packets before declaring loss.
static constexpr uint32_t kPacketThreshold = 3;

// Time threshold = 9/8 * RTT.
static constexpr uint32_t kTimeThresholdNum = 9;
static constexpr uint32_t kTimeThresholdDen = 8;

// ============================================================================
// ACK Policy
// ============================================================================

// Bounded ACK-of-ACK budget per PN space: replies to ACK-only packets in
// Initial/Handshake are the sole recovery channel for peers that only send
// ACK-only handshake packets after losing their Finished (quinn), but must be
// capped so two quicx endpoints don't ACK each other's ACK-only packets
// forever. Reset by any ack-eliciting packet in the same space.
static constexpr uint32_t kMaxAckOnlyReplies = 3;

// RFC 9000 §13.2.1 para 6: "an endpoint SHOULD acknowledge at least every
// second ACK-only packet" it receives. Consecutive ACK-only packets in the
// Application space are the peer's only loss-detection clock when our own
// data/response datagram was lost. Keyed on *consecutive* ACK-only packets;
// any ack-eliciting packet resets the counter, so a quicx<->quicx exchange
// self-terminates instead of ping-ponging.
static constexpr uint32_t kAckOnlySeqThreshold = 2;

// ============================================================================
// Connection Timers
// ============================================================================

// Idle timeout used while a handshake has been sent but is not confirmed yet.
//
// The peer's own retransmission schedule is exponential: with a ~235 ms PTO it
// reaches a several-second gap after only a handful of rounds, and it is free
// to give up well after that. A negotiated idle timeout in the ~10 s range
// (12.8 s in the interop runs) fires *before* those later retries, so the
// server tears down a connection that was one packet away from completing.
// 30 s covers the peer's probe rounds with margin while staying bounded --
// this is a liveness window for one specific handshake phase, not a new
// default.
static constexpr uint32_t kHandshakeConfirmGraceMs = 30000;

// Floor for the keep-alive interval: fast enough to survive the interop
// runner's 5 s NAT-rebind cadence, cheap enough to be negligible on the wire.
static constexpr uint32_t kMinKeepAliveMs = 1000;

// ============================================================================
// Path Validation / Connection Migration
// ============================================================================

// RFC 9000 §8.2.4: an endpoint SHOULD abandon path validation based on a
// timer at least three times the current PTO. We use a fixed 6 s default that
// is comfortably above 3×PTO for typical Internet paths (PTO ≈ a few hundred
// ms) and bounds how long a stuck migration can linger before we give up and
// either fall back to the prior path or close.
static constexpr uint32_t kDefaultPathValidationTimeoutMs = 6000;

// Probe retry limits for path validation / migration.
static constexpr uint32_t kMaxProbeRetries = 5;
static constexpr uint32_t kInitialProbeDelayMs = 100;
static constexpr uint32_t kMaxProbeDelayMs = 2000;

// ============================================================================
// Connection ID Pool
// ============================================================================

// Local CID pool bounds: keep at least kMinLocalCIDPoolSize CIDs available,
// generate up to kMaxLocalCIDPoolSize in parallel.
static constexpr size_t kMinLocalCIDPoolSize = 3;  // Keep at least 3 CIDs in pool
static constexpr size_t kMaxLocalCIDPoolSize = 8;  // Generate up to 8 CIDs

// Stateless reset tokens advertised by the peer. Bounded so a peer that
// floods NEW_CONNECTION_ID frames cannot grow this without limit; the peer's
// own active_connection_id_limit already bounds how many CIDs are useful.
static constexpr size_t kMaxPeerResetTokens = 32;

// Peer-initiated streams created before stream_state_cb_ was installed,
// replayed by SetStreamStateCallback().
static constexpr size_t kMaxUnnotifiedRemoteStreams = 64;

// ============================================================================
// Anti-Amplification (RFC 9000 Section 8.1)
// ============================================================================

// Maximum amplification factor towards an unvalidated address (RFC 9000 §8.1
// mandates a limit of 3x).
static constexpr uint64_t kAmplificationFactor = 3;

// Initial credit granted when entering unvalidated state (~allows one
// 1200-byte PATH_CHALLENGE datagram before the 3x budget kicks in).
static constexpr uint64_t kDefaultInitialCredit = 400;

// Fraction of the amplification limit above which we consider asking the
// peer to validate via Retry (90%).
static constexpr double kNearLimitThreshold = 0.9;

// ============================================================================
// Crypto Stream (handshake)
// ============================================================================

// RFC 9000 §7.5: "an endpoint MAY discard the data or [...] generate a
// CRYPTO_BUFFER_EXCEEDED connection error". Initial keys are derived from a
// publicly known DCID, so anyone (including a spoofed source address) can
// inject CRYPTO frames before the handshake completes. Without a cap, a
// stream of frames at scattered high offsets grows the out-of-order buffer
// without bound.
//
// NB: deliberately distinct from kMaxOutOfOrderBytes (the *stream*-level
// limit); this is the handshake CRYPTO-stream limit.
static constexpr uint64_t kMaxCryptoOutOfOrderBytes = 64 * 1024;
static constexpr size_t kMaxCryptoOutOfOrderFrames = 512;

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONFIG_H
