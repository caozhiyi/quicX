#ifndef QUIC_CONGESTION_CONTROL_IF_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_IF_CONGESTION_CONTROL

#include <cstdint>
#include <memory>

namespace quicx {

namespace common {
class QlogTrace;
}

namespace quic {

struct CcConfigV2 {
    uint64_t initial_cwnd_bytes = 10 * 1460;
    uint64_t min_cwnd_bytes = 2 * 1460;
    uint64_t max_cwnd_bytes = 1000 * 1460;
    uint64_t mss_bytes = 1460;
    double beta = 0.5;         // cwnd *= beta on loss
    bool ecn_enabled = false;  // reserved
};

struct SentPacketEvent {
    uint64_t pn = 0;
    uint64_t bytes = 0;
    uint64_t sent_time = 0;
    bool is_retransmit = false;
};

struct AckEvent {
    uint64_t pn = 0;
    uint64_t bytes_acked = 0;
    uint64_t ack_time = 0;   // us (steady_clock) — real ACK arrival time
    uint64_t ack_delay = 0;  // us, already << exponent and max_ack_delay-clamped
    bool ecn_ce = false;
    // RFC 9002 §7.3.2: send time of the acknowledged packet (us); used by
    // recovery-exit checks to compare against recovery_start_time_. Defaults
    // to 0 for legacy callers (which prevents early/spurious recovery exit).
    uint64_t acked_packet_send_time = 0;
};;

struct LossEvent {
    uint64_t pn = 0;
    uint64_t bytes_lost = 0;
    uint64_t lost_time = 0;
};

class ICongestionControl {
public:
    virtual ~ICongestionControl() = default;

    virtual void Configure(const CcConfigV2& cfg) = 0;

    /**
     * @brief Reset the controller to its initial state (RFC 9000 §9.4).
     *
     * Called when the connection migrates to a validated new path: capacity
     * estimates, cwnd and recovery state from the OLD path are meaningless
     * (and typically catastrophically oversized after a NAT rebind blackout,
     * causing multi-MB retransmit floods that overflow the bottleneck).
     * bytes_in_flight is preserved: those packets are genuinely unacked.
     * Default no-op for controllers without an explicit implementation.
     */
    virtual void Reset() {}

    virtual void OnPacketSent(const SentPacketEvent& ev) = 0;
    virtual void OnPacketAcked(const AckEvent& ev) = 0;
    virtual void OnPacketLost(const LossEvent& ev) = 0;
    /**
     * @brief P1-P4: per-ACK RTT sample in microseconds (all four params).
     *
     * |latest_rtt_us| is the raw sample of this ACK; |min_rtt_us| /
     * |smoothed_rtt_us| come from the host RttCalculator (RFC 9002 §5.3 EWMA
     * already applied). Algorithms pick what they need and MUST NOT smooth
     * again — double smoothing made every controller lag one extra RTT
     * behind real RTT changes. |ack_delay_us| is the peer-reported (already
     * clamped) delay of this sample.
     */
    virtual void OnRoundTripSample(
        uint64_t latest_rtt_us, uint64_t min_rtt_us, uint64_t smoothed_rtt_us, uint64_t ack_delay_us) = 0;

    /**
     * @brief Account for packets discarded with a packet number space
     * (RFC 9002 §7 / Appendix A.10: OnPacketNumberSpaceDiscarded).
     *
     * When Initial/Handshake keys are dropped, the unacked bytes in those
     * spaces must leave bytes_in_flight — but they were NOT lost: firing
     * OnPacketLost for them wrongly collapses the cwnd. Default no-op so
     * controllers without the accounting keep compiling.
     */
    virtual void OnPacketsDiscarded(uint64_t discarded_bytes) { (void)discarded_bytes; }

    /**
     * @brief RFC 9002 §7.6: persistent congestion established.
     *
     * Every ack-eliciting packet sent across a duration exceeding
     * (smoothed_rtt + max(4*rttvar, kGranularity) + max_ack_delay) *
     * kPersistentCongestionThreshold was declared lost. MUST collapse the
     * congestion window to the minimum congestion window (kMinimumWindow =
     * 2 * max_datagram_size) and clear the recovery period, re-entering
     * slow start. bytes_in_flight is unaffected (those packets are still
     * unacked). Default no-op for controllers without an implementation.
     */
    virtual void OnPersistentCongestion() {}

    enum class SendState { kOk, kBlockedByCwnd, kBlockedByPacing };
    virtual SendState CanSend(uint64_t now, uint64_t& can_send_bytes) const = 0;

    virtual uint64_t GetCongestionWindow() const = 0;
    virtual uint64_t GetBytesInFlight() const = 0;
    // Pacing rate in bytes/sec (matches IPacer::OnPacingRateUpdated unit).
    virtual uint64_t GetPacingRateBytesPerSec() const = 0;
    virtual uint64_t NextSendTime(uint64_t now) const = 0;

    // Observability helpers
    virtual bool InSlowStart() const = 0;
    virtual bool InRecovery() const = 0;
    virtual uint64_t GetSsthresh() const = 0;

    // Qlog support
    virtual void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) = 0;
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONGESTION_CONTROL_IF_CONGESTION_CONTROL