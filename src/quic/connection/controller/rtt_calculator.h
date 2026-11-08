#ifndef QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR
#define QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR

#include <atomic>
#include <cstdint>
#include <limits>

#include "quic/config.h"

namespace quicx {
namespace quic {

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

    // ---- P1-P4 microsecond domain (consumed by congestion control) ----
    //
    // The millisecond domain above feeds PTO / loss detection and stays
    // byte-compatible with the pre-existing behavior. The us domain below
    // receives the SAME samples at full steady_clock resolution, so CC
    // algorithms observe sub-millisecond RTT changes (crosstraffic bursts,
    // loopback) instead of quantized millisecond steps. Both domains run the
    // identical RFC 9002 §5.3 EWMA independently.
    bool UpdateRttUs(uint64_t send_time_us, uint64_t now_us, uint64_t ack_delay_us);

    // Us-domain observers. GetMinRttUs() returns 0 until the first sample.
    uint64_t GetLatestRttUs() const { return latest_rtt_us_.load(std::memory_order_relaxed); }
    uint64_t GetRttVarUs() const { return rtt_var_us_.load(std::memory_order_relaxed); }
    uint64_t GetSmoothedRttUs() const { return smoothed_rtt_us_.load(std::memory_order_relaxed); }
    uint64_t GetMinRttUs() const {
        uint64_t v = min_rtt_us_.load(std::memory_order_relaxed);
        return v == std::numeric_limits<uint64_t>::max() ? 0 : v;  // 0 = no sample yet (CC-friendly)
    }

    void Reset();

    uint32_t GetPTOInterval(uint32_t max_ack_delay);

    uint32_t GetLatestRtt() { return latest_rtt_.load(std::memory_order_relaxed); }
    uint32_t GetRttVar() { return rtt_var_.load(std::memory_order_relaxed); }
    uint32_t GetMinRtt() { return min_rtt_.load(std::memory_order_relaxed); }
    uint32_t GetSmoothedRtt() { return smoothed_rtt_.load(std::memory_order_relaxed); }

    // RFC 9002 Section 6.2: PTO with exponential backoff
    uint32_t GetPTOWithBackoff(uint32_t max_ack_delay);
    void OnPTOExpired();

    // RFC 9002 §7.6.2: time of the first RTT sample (same ms clock the RTT
    // updates use). Persistent congestion MUST NOT be established from
    // packets sent before any RTT sample existed (pre-sample PTO is derived
    // from the initial RTT and can be far off). 0 = no sample yet.
    uint64_t GetFirstRttSampleTime() const { return first_rtt_sample_time_.load(std::memory_order_relaxed); }
    void OnPacketAcked();
    // RFC 9002 A.11 OnPacketNumberSpaceDiscarded: "pto_count = 0" — dropping
    // Initial/Handshake keys is forward progress; the backoff accumulated
    // while probing those spaces must not tax the surviving space.
    void OnPacketNumberSpaceDiscarded() {
        pto_count_.store(0, std::memory_order_relaxed);
    }
    uint32_t GetConsecutivePTOCount() const { return consecutive_pto_count_.load(std::memory_order_relaxed); }

    // RFC 9000 §4.1.2: the handshake is confirmed once the peer acknowledges a
    // packet we sent at the 1-RTT level. Until then it is unconfirmed, and the
    // probe cadence is deliberately kept dense (see kMaxPTOBackoffUnconfirmed).
    // Idempotent; safe to call on every ACK.
    void SetHandshakeConfirmed() { handshake_confirmed_.store(true, std::memory_order_relaxed); }
    bool IsHandshakeConfirmed() const { return handshake_confirmed_.load(std::memory_order_relaxed); }

private:
    // All RTT/PTO state is accessed from both the ACK-processing path (worker
    // thread) and the PTO timer / teardown path (event-loop / timer thread), so
    // every field is atomic. TSan flagged a real data race on
    // consecutive_pto_count_ between GetConsecutivePTOCount() (read in the
    // connection destructor) and OnPTOExpired() (write on the timer thread).
    std::atomic<uint32_t> latest_rtt_{0};
    std::atomic<uint32_t> rtt_var_{0};
    std::atomic<uint32_t> min_rtt_{0};
    std::atomic<uint32_t> smoothed_rtt_{0};

    std::atomic<uint64_t> last_update_time_{0};
    std::atomic<uint64_t> first_rtt_sample_time_{0};

    // P1-P4 us-domain mirror of the four EWMA states above.
    std::atomic<uint64_t> latest_rtt_us_{0};
    std::atomic<uint64_t> rtt_var_us_{0};
    std::atomic<uint64_t> min_rtt_us_{std::numeric_limits<uint64_t>::max()};
    std::atomic<uint64_t> smoothed_rtt_us_{0};
    std::atomic<uint64_t> last_update_time_us_{0};
    std::atomic<uint64_t> first_rtt_sample_time_us_{0};

    // RFC 9002: PTO backoff state
    std::atomic<uint32_t> pto_count_{0};              // Current backoff exponent
    std::atomic<uint32_t> consecutive_pto_count_{0};  // Count of consecutive PTOs without ACK
    std::atomic<bool> handshake_confirmed_{false};    // Peer has ACKed a 1-RTT packet
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR