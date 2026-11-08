// Copyright (c) 2024 The quicX Authors. All rights reserved.
// Congestion-control tunables, centralized for easy benchmarking/retuning.

#ifndef QUIC_CONGESTION_CONTROL_CC_CONFIG_H
#define QUIC_CONGESTION_CONTROL_CC_CONFIG_H

#include <cstddef>
#include <cstdint>

namespace quicx {
namespace quic {

// ============================================================================
// CUBIC (packets domain)
// ============================================================================

// CUBIC scaling constant C. Linux uses 0.4; the RFC draft suggests 0.4 as a
// balance between friendliness (towards Reno) and throughput.
static constexpr double kCubicC = 0.4;

// Multiplicative decrease factor (window scaling on loss).
static constexpr double kBetaCubic = 0.7;

// HyStart++ (RFC 9406): low cwnd threshold (in packets) below which the
// exit checks are skipped (window too small for the signals to be
// meaningful).
static constexpr double kHyStartLowWindow = 16.0;

// HyStart++ (RFC 9406 §4.3): min RTT samples per round before the
// delay-increase check is trusted (N_RTT_SAMPLE).
static constexpr uint32_t kHyStartMinSamples = 8;

// HyStart++ (RFC 9406 §4.3): RttThresh = clamp(lastRoundMinRTT / 8,
// [MIN_RTT_THRESH, MAX_RTT_THRESH]) — the per-round min-RTT growth that
// indicates a queue is starting to build (MIN_RTT_DIVISOR = 8).
static constexpr uint32_t kHyStartRttThreshUs = 4000;   // MIN_RTT_THRESH
static constexpr uint32_t kHyStartMaxRttThreshUs = 16000;  // MAX_RTT_THRESH
static constexpr uint32_t kHyStartRttDivisor = 8;       // MIN_RTT_DIVISOR

// HyStart++ CSS (Conservative Slow Start): cwnd grows at 1/4 rate for at
// most 5 rounds after the delay-increase signal, reverting to slow start
// if the RTT inflation turns out to be transient (RFC 9406 §4.2).
static constexpr uint32_t kCssRounds = 5;            // CSS_ROUNDS
static constexpr uint32_t kCssGrowthDivisor = 4;     // CSS_GROWTH_DIVISOR

// NOTE (P1-CC4): RFC 9406 deliberately dropped the original HyStart
// "Inter-Packet Arrival" (ACK train) signal — "In practice, the
// Inter-Packet Arrival algorithm does not perform well and is not able to
// detect congestion early, primarily due to ACK compression" (§4.1). The
// old kHyStartAckDeltaUs constant and its (inverted) comparison were
// removed with it.

// ============================================================================
// BBR
// ============================================================================

// Max-filter window size for the bandwidth estimate (~10 samples).
static constexpr size_t kBwWindow = 10;

// ProbeRTT: enter ProbeRTT when the min_rtt sample is older than this.
static constexpr uint64_t kProbeRttIntervalUs = 10ull * 1000ull * 1000ull;  // 10s

// ProbeRTT: how long to hold the reduced window while draining.
static constexpr uint64_t kProbeRttTimeUs = 200ull * 1000ull;  // 200ms

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONGESTION_CONTROL_CC_CONFIG_H
