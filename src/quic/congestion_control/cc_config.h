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

// HyStart: low cwnd threshold (in packets) below which HyStart exit checks
// are skipped (window too small for the signals to be meaningful).
static constexpr double kHyStartLowWindow = 16.0;

// HyStart: min RTT samples needed before the exit checks are trusted.
static constexpr uint32_t kHyStartMinSamples = 8;

// HyStart: RTT increase threshold (4ms) above the round-trip min that
// indicates a queue is starting to build.
static constexpr uint32_t kHyStartRttThreshUs = 4000;

// HyStart: ACK train threshold (2ms); inter-ACK delay above this suggests
// upstream queuing on the reverse path.
static constexpr uint32_t kHyStartAckDeltaUs = 2000;

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
