#ifndef QUIC_CONGESTION_CONTROL_BBR_V2_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_BBR_V2_CONGESTION_CONTROL

#include <deque>
#include <memory>
#include <cstdint>

#include "quic/congestion_control/if_pacer.h"
#include "quic/congestion_control/if_congestion_control.h"


namespace quicx {
namespace quic {


class BBRv2CongestionControl : public ICongestionControl {
public:
    BBRv2CongestionControl();
    ~BBRv2CongestionControl() override = default;

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

    bool InSlowStart() const override { return mode_ == Mode::kStartup; }
    bool InRecovery() const override { return false; }
    uint64_t GetSsthresh() const override { return ssthresh_bytes_; }

private:
    enum class Mode { kStartup, kDrain, kProbeBw, kProbeRtt };

    struct BwSample { uint64_t time_us; uint64_t bytes_per_sec; };

    // Helpers
    void MaybeEnterOrExitProbeRtt(uint64_t now_us);
    void AdvanceProbeBwCycle(uint64_t now_us);
    void UpdateMaxBandwidth(uint64_t sample_bps, uint64_t now_us);
    void CheckStartupFullBandwidth(uint64_t now_us);
    void UpdatePacingRate();

    uint64_t BdpBytes(uint64_t gain_num, uint64_t gain_den) const; // BDP * gain

    // Config
    CcConfigV2 cfg_{};

    // State
    Mode mode_ = Mode::kStartup;
    uint64_t cwnd_bytes_ = 0;
    uint64_t ssthresh_bytes_ = UINT64_MAX;
    uint64_t bytes_in_flight_ = 0;

    // RTT
    uint64_t srtt_us_ = 0;
    uint64_t min_rtt_us_ = 0;
    uint64_t min_rtt_stamp_us_ = 0;

    // Bandwidth filter
    static constexpr size_t kBwWindow = 10;
    std::deque<BwSample> bw_window_;
    uint64_t max_bw_bps_ = 0;

    // Gains
    double pacing_gain_ = 2.885; // STARTUP
    double cwnd_gain_ = 2.0;

    // ProbeBW cycle
    int cycle_index_ = 0;
    uint64_t cycle_start_us_ = 0;

    // ProbeRTT
    static constexpr uint64_t kProbeRttIntervalUs = 10ull * 1000ull * 1000ull; // 10s
    static constexpr uint64_t kProbeRttTimeUs = 200ull * 1000ull;              // 200ms
    bool probe_rtt_done_stamp_valid_ = false;
    uint64_t probe_rtt_done_stamp_us_ = 0;

    // BBRv2 inflight limits (bytes)
    uint64_t inflight_hi_bytes_ = UINT64_MAX;
    uint64_t inflight_lo_bytes_ = 0;

    // Loss tracking (very lightweight)
    uint64_t loss_event_count_in_round_ = 0;
    
    // Full bandwidth detection (instance variables, not static!)
    uint64_t full_bw_bps_ = 0;
    int full_bw_cnt_ = 0;

    // Aggregated bandwidth sampling (bytes over interval)
    uint64_t bw_sample_start_us_ = 0;
    uint64_t bw_sample_bytes_acc_ = 0;

    // Pacer
    std::unique_ptr<IPacer> pacer_;
};

} // namespace quic
} // namespace quicx

#endif


