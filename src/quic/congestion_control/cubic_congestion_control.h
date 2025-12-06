#ifndef QUIC_CONGESTION_CONTROL_CUBIC_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_CUBIC_CONGESTION_CONTROL

#include <memory>
#include <cstdint>

#include "quic/congestion_control/if_pacer.h"
#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {

class CubicCongestionControl : public ICongestionControl {
public:
    CubicCongestionControl();
    ~CubicCongestionControl() override = default;

    void Configure(const CcConfigV2& cfg) override;
    void OnPacketSent(const SentPacketEvent& ev) override;
    void OnPacketAcked(const AckEvent& ev) override;
    void OnPacketLost(const LossEvent& ev) override;
    void OnRoundTripSample(uint64_t latest_rtt, uint64_t ack_delay = 0) override;

    SendState CanSend(uint64_t now, uint64_t& can_send_bytes) const override;

    uint64_t GetCongestionWindow() const override { return cwnd_bytes_; }
    uint64_t GetBytesInFlight() const override { return bytes_in_flight_; }
    uint64_t GetPacingRateBps() const override;
    uint64_t NextSendTime(uint64_t now) const override;

    bool InSlowStart() const override { return in_slow_start_; }
    bool InRecovery() const override { return in_recovery_; }
    uint64_t GetSsthresh() const override { return ssthresh_bytes_; }

    void SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) override;

private:
    void ResetEpoch(uint64_t now);
    void IncreaseOnAck(uint64_t bytes_acked, uint64_t now);
    void EnterRecovery(uint64_t now);
    
    // HyStart helpers
    void ResetHyStart();
    bool CheckHyStartExit(uint64_t latest_rtt, uint64_t now);

    // Constants for CUBIC (in packets domain)
    static constexpr double kCubicC = 0.4;     // cubic scaling constant
    static constexpr double kBetaCubic = 0.7;  // multiplicative decrease factor
    
    // HyStart constants
    static constexpr double kHyStartLowWindow = 16.0;    // Low cwnd threshold (in packets)
    static constexpr uint32_t kHyStartMinSamples = 8;    // Min RTT samples needed
    static constexpr uint32_t kHyStartRttThreshUs = 4000; // 4ms RTT increase threshold
    static constexpr uint32_t kHyStartAckDeltaUs = 2000; // 2ms ACK train threshold

    // Config
    CcConfigV2 cfg_{};

    // State (bytes)
    uint64_t cwnd_bytes_ = 0;
    uint64_t bytes_in_flight_ = 0;
    uint64_t ssthresh_bytes_ = UINT64_MAX;

    // RTT (microseconds)
    uint64_t srtt_us_ = 0;

    // CUBIC specific (packets domain and time in microseconds)
    double w_max_pkts_ = 0.0;          // cwnd before last loss (in packets)
    double w_last_pkts_ = 0.0;         // last cwnd in packets
    uint64_t epoch_start_us_ = 0;      // epoch start time
    double k_time_sec_ = 0.0;          // K in seconds

    // Recovery bookkeeping
    bool in_slow_start_ = true;
    bool in_recovery_ = false;
    uint64_t recovery_start_time_us_ = 0;
    
    // HyStart state
    bool hystart_enabled_ = true;
    uint64_t hystart_min_rtt_us_ = UINT64_MAX;
    uint64_t hystart_rtt_sample_count_ = 0;
    uint64_t hystart_current_round_min_rtt_us_ = UINT64_MAX;
    uint64_t hystart_last_round_min_rtt_us_ = UINT64_MAX;
    uint64_t hystart_round_start_us_ = 0;
    uint64_t hystart_last_ack_time_us_ = 0;
    bool hystart_found_exit_ = false;

    // Pacer
    std::unique_ptr<IPacer> pacer_;

    // Qlog
    std::shared_ptr<common::QlogTrace> qlog_trace_;
};

} // namespace quic
} // namespace quicx

#endif


