#ifndef QUIC_CONGESTION_CONTROL_CUBIC_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_CUBIC_CONGESTION_CONTROL

#include <cstdint>
#include <memory>

#include "quic/congestion_control/cc_config.h"
#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/if_pacer.h"

namespace quicx {
namespace quic {

class CubicCongestionControl: public ICongestionControl {
public:
    CubicCongestionControl();
    ~CubicCongestionControl() override = default;

    void Configure(const CcConfigV2& cfg) override;
    void Reset() override;
    void OnPacketSent(const SentPacketEvent& ev) override;
    void OnPacketAcked(const AckEvent& ev) override;
    void OnPacketLost(const LossEvent& ev) override;
    void OnRoundTripSample(
        uint64_t latest_rtt_us, uint64_t min_rtt_us, uint64_t smoothed_rtt_us, uint64_t ack_delay_us) override;

    SendState CanSend(uint64_t now, uint64_t& can_send_bytes) const override;

    uint64_t GetCongestionWindow() const override { return cwnd_bytes_; }
    uint64_t GetBytesInFlight() const override { return bytes_in_flight_; }
    // RFC 9002 §7: bytes discarded with a packet number space leave
    // bytes_in_flight without being declared lost.
    void OnPacketsDiscarded(uint64_t discarded_bytes) override {
        bytes_in_flight_ = (bytes_in_flight_ > discarded_bytes) ? bytes_in_flight_ - discarded_bytes : 0;
    }

    // RFC 9002 §7.6: persistent congestion — collapse cwnd to the minimum
    // window, clear the recovery period and re-enter slow start.
    void OnPersistentCongestion() override;
    uint64_t GetPacingRateBytesPerSec() const override;
    uint64_t NextSendTime(uint64_t now) const override;

    bool InSlowStart() const override { return in_slow_start_; }
    bool InRecovery() const override { return in_recovery_; }
    // P1-CC4: HyStart++ CSS state (RFC 9406 §4.2), exposed for tests.
    bool InCss() const { return in_css_; }
    uint32_t GetCssRounds() const { return css_rounds_; }
    // Diagnostics for HyStart++ tests.
    uint64_t DiagSampleCount() const { return hystart_rtt_sample_count_; }
    uint64_t DiagCurrRoundMin() const { return hystart_curr_round_min_rtt_us_; }
    uint64_t DiagLastRoundMin() const { return hystart_last_round_min_rtt_us_; }
    uint64_t DiagWindowEndPn() const { return hystart_window_end_pn_; }
    uint64_t DiagLastSentPn() const { return hystart_last_sent_pn_; }
    uint64_t GetSsthresh() const override { return ssthresh_bytes_; }
    // CUBIC W_max in packets (RFC 9438 §4.1.2); exposed for tests.
    double GetWMaxPkts() const { return w_max_pkts_; }

    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) override;

private:
    void ResetEpoch(uint64_t now);
    void IncreaseOnAck(uint64_t bytes_acked, uint64_t now);
    // P1-CC4: single multiplicative-decrease entry shared by the loss and
    // ECN-CE paths (the two branches used to be line-for-line duplicates).
    void EnterRecovery(uint64_t now);

    // HyStart++ (RFC 9406) helpers
    void ResetHyStart();
    // Per-ACK update while in slow start / CSS: round tracking and the
    // delay-increase check. Returns true when it exits slow start into CSS.
    void HyStartOnAck();
    // Round boundary (RFC 9406 §4.2: a round ends when the packet sent at
    // the round's window end is ACKed): rotate min-RTT rounds, count CSS
    // rounds, and commit to congestion avoidance when CSS expires.
    void HyStartOnRoundEnd();

    // Config
    CcConfigV2 cfg_{};

    // State (bytes)
    uint64_t cwnd_bytes_ = 0;
    uint64_t bytes_in_flight_ = 0;
    uint64_t ssthresh_bytes_ = UINT64_MAX;

    // RTT (microseconds)
    uint64_t srtt_us_ = 0;
    // P1-P4: raw latest RTT sample (us) — HyStart's per-round min-RTT
    // tracking needs the unsmoothed sample, not the EWMA.
    uint64_t latest_rtt_us_ = 0;

    // CUBIC specific (packets domain and time in microseconds)
    double w_max_pkts_ = 0.0;      // cwnd before last loss (in packets)
    double w_last_pkts_ = 0.0;     // last cwnd in packets
    uint64_t epoch_start_us_ = 0;  // epoch start time
    double k_time_sec_ = 0.0;      // K in seconds

    // Recovery bookkeeping
    bool in_slow_start_ = true;
    bool in_recovery_ = false;
    uint64_t recovery_start_time_us_ = 0;

    // ---- HyStart++ state (RFC 9406) ----
    bool hystart_enabled_ = true;
    // Rounds are delimited by packet number, not time: window_end_pn is the
    // highest sent PN when the round opened; the round ends once it is ACKed.
    uint64_t hystart_window_end_pn_ = 0;
    uint64_t hystart_last_sent_pn_ = 0;
    uint64_t hystart_curr_round_min_rtt_us_ = UINT64_MAX;
    uint64_t hystart_last_round_min_rtt_us_ = UINT64_MAX;
    uint64_t hystart_rtt_sample_count_ = 0;
    // CSS (Conservative Slow Start): entered on the delay-increase signal —
    // cwnd grows at 1/4 rate; a transient RTT spike reverts to slow start,
    // CSS_ROUNDS expiry or any loss/ECN commits to congestion avoidance.
    bool in_css_ = false;
    uint32_t css_rounds_ = 0;
    uint64_t css_baseline_min_rtt_us_ = UINT64_MAX;
    // P1-CC4: app-limited freeze — packets sent while less than half the
    // window was in flight do not grow the cwnd when ACKed (cwnd must
    // reflect the path capacity, not the application's send pattern).
    uint64_t app_limited_until_pn_ = 0;

    // Pacer
    std::unique_ptr<IPacer> pacer_;

    // Qlog
    std::shared_ptr<common::QlogTrace> qlog_trace_;
};

}  // namespace quicx
}  // namespace quicx

#endif  // QUIC_CONGESTION_CONTROL_CUBIC_CONGESTION_CONTROL
