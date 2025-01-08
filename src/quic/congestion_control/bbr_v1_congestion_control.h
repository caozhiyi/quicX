#ifndef QUIC_CONGESTION_CONTROL_BBR_V1_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_BBR_V1_CONGESTION_CONTROL

#include <deque>
#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {

// BBR v1 congestion control implementation
class BBRv1CongestionControl:
    public ICongestionControl {
public:
    BBRv1CongestionControl();
    ~BBRv1CongestionControl() override;

    void OnPacketSent(size_t bytes, uint64_t sent_time) override;
    void OnPacketAcked(size_t bytes, uint64_t ack_time) override;
    void OnPacketLost(size_t bytes, uint64_t lost_time) override;
    void OnRttUpdated(uint64_t rtt) override;
    size_t GetCongestionWindow() const override;
    size_t GetBytesInFlight() const override;
    bool CanSend(uint64_t now, uint32_t& can_send_bytes) const override;
    uint64_t GetPacingRate() const override;
    void Reset() override;

private:
    // BBR modes
    enum Mode {
        STARTUP,    // Startup phase
        DRAIN,      // Drain phase
        PROBE_BW,   // Probe bandwidth phase
        PROBE_RTT   // Probe RTT phase
    };

    // BBR specific parameters
    static constexpr double HIGH_GAIN = 2.885;
    static constexpr double DRAIN_GAIN = 1.0 / HIGH_GAIN;
    static constexpr double PACING_GAIN = 1.25;
    static constexpr double LOW_GAIN = 0.75;
    static constexpr size_t MIN_WINDOW = 4 * 1460;
    static constexpr uint64_t PROBE_RTT_INTERVAL = 10000000; // 10s in microseconds
    static constexpr uint64_t PROBE_RTT_DURATION = 200000;   // 200ms in microseconds

    // BBR state variables
    Mode mode_;
    uint64_t min_rtt_timestamp_;
    uint64_t probe_rtt_done_timestamp_;
    bool probe_rtt_round_done_;
    double pacing_gain_;
    double cwnd_gain_;
    
    // Bandwidth estimation
    struct BandwidthSample {
        uint64_t bandwidth;
        uint64_t timestamp;
    };
    std::deque<BandwidthSample> bandwidth_samples_;
    uint64_t max_bandwidth_;
    
    // Helper methods
    void UpdateBandwidth(uint64_t bandwidth, uint64_t timestamp);
    void EnterStartup();
    void EnterDrain();
    void EnterProbeBW();
    void EnterProbeRTT();
    void CheckCyclePhase(uint64_t now);
    void UpdateGains();
    uint64_t GetTargetCwnd() const;
};

}
}

#endif
