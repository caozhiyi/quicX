#ifndef QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR
#define QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR

#include <atomic>
#include <cstdint>

namespace quicx {
namespace quic {

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

// Returns the process-wide initial RTT in milliseconds. Used by RttCalculator
// on construction / Reset() to seed smoothed_rtt_ before any RTT sample is
// available. Thread-safe.
uint32_t GetDefaultInitialRtt();

// Override the process-wide initial RTT (in milliseconds). Thread-safe; intended
// for test harnesses and benchmarks running against loopback or LAN peers where
// the default 250 ms baseline unnecessarily inflates every fresh handshake's
// first-sample PTO window. Passing 0 resets to the built-in default.
//
// **Do not** lower this for real-network deployments: a too-small initial PTO
// causes spurious retransmits on the *second* hop of any handshake on a
// transcontinental RTT.
void SetDefaultInitialRtt(uint32_t ms);

class RttCalculator {
public:
    RttCalculator();
    ~RttCalculator();

    bool UpdateRtt(uint64_t send_time, uint64_t now, uint64_t ack_delay);

    void Reset();

    uint32_t GetPT0Interval(uint32_t max_ack_delay);

    uint32_t GetLatestRtt() { return latest_rtt_; }
    uint32_t GetRttVar() { return rtt_var_; }
    uint32_t GetMinRtt() { return min_rtt_; }
    uint32_t GetSmoothedRtt() { return smoothed_rtt_; }

    // RFC 9002 Section 6.2: PTO with exponential backoff
    uint32_t GetPTOWithBackoff(uint32_t max_ack_delay);
    void OnPTOExpired();
    void OnPacketAcked();
    uint32_t GetConsecutivePTOCount() const { return consecutive_pto_count_; }

    static constexpr uint32_t kMaxPTOBackoff = 6;        // Max 2^6 = 64x backoff
    static constexpr uint32_t kMaxConsecutivePTOs = 16;  // ~3 PTO cycles for idle timeout

private:
    uint32_t latest_rtt_;
    uint32_t rtt_var_;
    uint32_t min_rtt_;
    uint32_t smoothed_rtt_;

    uint64_t last_update_time_;

    // RFC 9002: PTO backoff state
    uint32_t pto_count_ = 0;              // Current backoff exponent
    uint32_t consecutive_pto_count_ = 0;  // Count of consecutive PTOs without ACK
};

}  // namespace quic
}  // namespace quicx

#endif