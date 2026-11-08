#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

#include "common/log/log.h"

#include "quic/connection/controller/rtt_calculator.h"

namespace quicx {
namespace quic {

namespace {
// Process-wide initial-RTT override. Defaults to kInitRttDefaultMs; can be
// lowered by tests/benchmarks via SetDefaultInitialRtt() to avoid the ~1 s
// first-packet PTO cliff on loopback paths (see docs/internal/perf_e2e_analysis.md §6
// P3). Stored as std::atomic so calls are safe from any thread, including
// unit-test setup running concurrently with connection teardown.
std::atomic<uint32_t> g_initial_rtt_override{kInitRttDefaultMs};
}  // namespace

uint32_t GetDefaultInitialRtt() {
    return g_initial_rtt_override.load(std::memory_order_relaxed);
}

void SetDefaultInitialRtt(uint32_t ms) {
    // Bound the override: a zero/absurdly-small value would make PTO = 0 which
    // fires the retransmit timer immediately and causes a livelock of PTO
    // probes. Anything above the default maps back to the default to prevent
    // misuse as a *longer* timeout (which is not what the knob is for).
    if (ms == 0 || ms > kInitRttDefaultMs) {
        g_initial_rtt_override.store(kInitRttDefaultMs, std::memory_order_relaxed);
        return;
    }
    g_initial_rtt_override.store(ms, std::memory_order_relaxed);
}

RttCalculator::RttCalculator() {
    Reset();
}

RttCalculator::~RttCalculator() {}

bool RttCalculator::UpdateRtt(uint64_t send_time, uint64_t now, uint64_t ack_delay) {
    LOG_DEBUG("update rtt. send time:%lld, now:%lld, ack delay:%d", send_time, now, ack_delay);

    uint32_t latest_rtt = static_cast<uint32_t>(now - send_time);
    // first update rtt
    if (last_update_time_.load(std::memory_order_relaxed) == 0) {
        min_rtt_.store(latest_rtt, std::memory_order_relaxed);
        smoothed_rtt_.store(latest_rtt, std::memory_order_relaxed);
        rtt_var_.store(latest_rtt >> 1, std::memory_order_relaxed);
        first_rtt_sample_time_.store(now, std::memory_order_relaxed);

    } else {
        uint32_t cur_min = min_rtt_.load(std::memory_order_relaxed);
        uint32_t min_rtt = std::min(cur_min, latest_rtt);
        min_rtt_.store(min_rtt, std::memory_order_relaxed);

        // RFC 9002 §5.3: SHOULD ignore the peer's max_ack_delay until the
        // handshake is confirmed; MUST use min(ack_delay, peer's max_ack_delay)
        // afterwards. (Not yet enforced here — handshake_confirmed plumbing is
        // tracked in §2 of the roadmap.)
        uint32_t adjusted_rtt = latest_rtt;
        if (latest_rtt >= (min_rtt + ack_delay)) {
            adjusted_rtt -= ack_delay;
        }

        // smoothed_rtt = 7/8 * smoothed_rtt + 1/8 * adjusted_rtt
        uint32_t srtt = smoothed_rtt_.load(std::memory_order_relaxed);
        smoothed_rtt_.store(srtt - (srtt >> 3) + (adjusted_rtt >> 3), std::memory_order_relaxed);

        // rttvar_sample = abs(smoothed_rtt - adjusted_rtt)
        uint32_t srtt2 = smoothed_rtt_.load(std::memory_order_relaxed);
        uint32_t rttvar_sample = srtt2 > adjusted_rtt ? srtt2 - adjusted_rtt : adjusted_rtt - srtt2;

        // rttvar = 3/4 * rttvar + 1/4 * rttvar_sample
        uint32_t rv = rtt_var_.load(std::memory_order_relaxed);
        rtt_var_.store(rv - (rv >> 2) + (rttvar_sample >> 2), std::memory_order_relaxed);
    }
    latest_rtt_.store(latest_rtt, std::memory_order_relaxed);
    last_update_time_.store(now, std::memory_order_relaxed);

    return true;
}

// P1-P4: microsecond mirror of UpdateRtt (RFC 9002 §5.3, identical EWMA).
// Consumed by congestion control via Get*Us(); the ms domain above keeps
// feeding PTO / loss detection unchanged.
bool RttCalculator::UpdateRttUs(uint64_t send_time_us, uint64_t now_us, uint64_t ack_delay_us) {
    uint64_t latest_rtt_us = (now_us > send_time_us) ? now_us - send_time_us : 0;
    if (latest_rtt_us == 0) {
        latest_rtt_us = 1;  // loopback: sub-tick sample still counts as 1us
    }

    if (last_update_time_us_.load(std::memory_order_relaxed) == 0) {
        min_rtt_us_.store(latest_rtt_us, std::memory_order_relaxed);
        smoothed_rtt_us_.store(latest_rtt_us, std::memory_order_relaxed);
        rtt_var_us_.store(latest_rtt_us >> 1, std::memory_order_relaxed);
        first_rtt_sample_time_us_.store(now_us, std::memory_order_relaxed);
    } else {
        uint64_t cur_min = min_rtt_us_.load(std::memory_order_relaxed);
        uint64_t min_rtt = std::min(cur_min, latest_rtt_us);
        min_rtt_us_.store(min_rtt, std::memory_order_relaxed);

        // Same adjusted_rtt rule as the ms domain: subtract the (already
        // max_ack_delay-clamped) peer ack delay only when it cannot drag the
        // sample below the observed minimum.
        uint64_t adjusted_rtt = latest_rtt_us;
        if (latest_rtt_us >= (min_rtt + ack_delay_us)) {
            adjusted_rtt -= ack_delay_us;
        }

        uint64_t srtt = smoothed_rtt_us_.load(std::memory_order_relaxed);
        smoothed_rtt_us_.store(srtt - (srtt >> 3) + (adjusted_rtt >> 3), std::memory_order_relaxed);

        uint64_t srtt2 = smoothed_rtt_us_.load(std::memory_order_relaxed);
        uint64_t rttvar_sample = srtt2 > adjusted_rtt ? srtt2 - adjusted_rtt : adjusted_rtt - srtt2;

        uint64_t rv = rtt_var_us_.load(std::memory_order_relaxed);
        rtt_var_us_.store(rv - (rv >> 2) + (rttvar_sample >> 2), std::memory_order_relaxed);

        if (smoothed_rtt_us_.load(std::memory_order_relaxed) == 0) {
            smoothed_rtt_us_.store(1, std::memory_order_relaxed);  // EWMA rounding guard
        }
    }
    latest_rtt_us_.store(latest_rtt_us, std::memory_order_relaxed);
    last_update_time_us_.store(now_us, std::memory_order_relaxed);
    return true;
}

void RttCalculator::Reset() {
    latest_rtt_.store(0, std::memory_order_relaxed);
    // Seed SRTT from the process-level override; in production this returns
    // the RFC-friendly default (kInitRttDefaultMs = 250 ms). Benchmarks that
    // run exclusively against loopback can opt in to a smaller value via
    // SetDefaultInitialRtt() to avoid a ~1 s cold-start PTO cliff.
    uint32_t init_rtt = GetDefaultInitialRtt();
    smoothed_rtt_.store(init_rtt, std::memory_order_relaxed);
    rtt_var_.store(init_rtt / 2, std::memory_order_relaxed);
    min_rtt_.store(std::numeric_limits<uint32_t>::max(), std::memory_order_relaxed);

    last_update_time_.store(0, std::memory_order_relaxed);
    first_rtt_sample_time_.store(0, std::memory_order_relaxed);

    // P1-P4: reset the us-domain mirror alongside the ms domain.
    latest_rtt_us_.store(0, std::memory_order_relaxed);
    rtt_var_us_.store(0, std::memory_order_relaxed);
    min_rtt_us_.store(std::numeric_limits<uint64_t>::max(), std::memory_order_relaxed);
    smoothed_rtt_us_.store(0, std::memory_order_relaxed);
    last_update_time_us_.store(0, std::memory_order_relaxed);
    first_rtt_sample_time_us_.store(0, std::memory_order_relaxed);
}

uint32_t RttCalculator::GetPTOInterval(uint32_t max_ack_delay) {
    // PTO = smoothed_rtt + max(4*rttvar, kGranularity) + max_ack_delay
    // kGranularity is 1ms, so use 1 instead of 1000
    return smoothed_rtt_.load(std::memory_order_relaxed) +
           std::max<uint32_t>(rtt_var_.load(std::memory_order_relaxed) << 2, 1) + max_ack_delay;
}

// RFC 9002 Section 6.2: PTO with exponential backoff
uint32_t RttCalculator::GetPTOWithBackoff(uint32_t max_ack_delay) {
    uint32_t base_pto = GetPTOInterval(max_ack_delay);

    // Apply exponential backoff: PTO * (2 ^ pto_count)
    // The exponent is capped much lower while the handshake is unconfirmed so
    // the probe sequence stays dense enough to matter inside the connection's
    // own idle timeout -- see kMaxPTOBackoffUnconfirmed.
    uint32_t cap = IsHandshakeConfirmed() ? kMaxPTOBackoff : kMaxPTOBackoffUnconfirmed;
    uint32_t backoff_exp = std::min(pto_count_.load(std::memory_order_relaxed), cap);
    return base_pto << backoff_exp;  // Equivalent to base_pto * (2 ^ backoff_exp)
}

void RttCalculator::OnPTOExpired() {
    // Increment backoff for next PTO, capped at kMaxPTOBackoff
    pto_count_.store(
        std::min(pto_count_.load(std::memory_order_relaxed) + 1, kMaxPTOBackoff), std::memory_order_relaxed);
    consecutive_pto_count_.fetch_add(1, std::memory_order_relaxed);

    LOG_DEBUG("PTO expired: pto_count=%u, consecutive_pto_count=%u", pto_count_.load(std::memory_order_relaxed),
        consecutive_pto_count_.load(std::memory_order_relaxed));
}

void RttCalculator::OnPacketAcked() {
    // Reset backoff when we receive an ACK
    if (pto_count_.load(std::memory_order_relaxed) > 0 || consecutive_pto_count_.load(std::memory_order_relaxed) > 0) {
        LOG_DEBUG("Packet ACKed: resetting PTO backoff (was pto_count=%u, consecutive=%u)",
            pto_count_.load(std::memory_order_relaxed), consecutive_pto_count_.load(std::memory_order_relaxed));
    }
    pto_count_.store(0, std::memory_order_relaxed);
    consecutive_pto_count_.store(0, std::memory_order_relaxed);
}

}  // namespace quic
}  // namespace quicx
