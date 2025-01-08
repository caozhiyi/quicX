#ifndef QUIC_CONGESTION_CONTROL_CUBIC_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_CUBIC_CONGESTION_CONTROL

#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {

class CubicCongestionControl:
    public ICongestionControl {
public:
    CubicCongestionControl();
    ~CubicCongestionControl() override;

    void OnPacketSent(size_t bytes, uint64_t sent_time) override;
    void OnPacketAcked(size_t bytes, uint64_t ack_time) override;
    void OnPacketLost(size_t bytes, uint64_t lost_time) override;
    void OnRttUpdated(uint64_t rtt) override;
    size_t GetCongestionWindow() const override;
    size_t GetBytesInFlight() const override;
    bool CanSend(uint64_t now, uint32_t& can_send_bytes) const override;
    uint64_t GetPacingRate() const override;
    void Reset() override;
    size_t GetInitialWindow() const { return MIN_WINDOW; }

private:
    // CUBIC specific parameters
    static constexpr double BETA_CUBIC = 0.7;    // Beta for multiplicative decrease
    static constexpr double C = 0.4;             // Cubic scaling factor
    static constexpr size_t MIN_WINDOW = 2 * 1460; // Minimum window size
    
    // CUBIC state variables
    uint64_t epoch_start_;       // Time when current epoch started
    size_t w_max_;               // Window size before last reduction
    size_t w_last_max_;          // Last maximum window size
    size_t k_;                   // Time period until window grows to w_max
    double origin_point_;        // Origin point of cubic function
    bool tcp_friendliness_;      // Enable TCP friendliness

    // Helper methods
    void UpdateCubicParameters(uint64_t current_time);
    size_t CubicWindowSize(uint64_t elapsed_time);
    size_t TcpFriendlyWindowSize();
};

}
}

#endif
