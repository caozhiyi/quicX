#ifndef QUIC_CONGESTION_CONTROL_RENO_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_RENO_CONGESTION_CONTROL

#include "quic/congestion_control/if_congestion_control.h"

namespace quicx {
namespace quic {

class RenoCongestionControl:
    public ICongestionControl {
public:
    RenoCongestionControl();
    ~RenoCongestionControl() override;

    void OnPacketSent(size_t bytes, uint64_t sent_time) override;
    void OnPacketAcked(size_t bytes, uint64_t ack_time) override;
    void OnPacketLost(size_t bytes, uint64_t lost_time) override;
    void OnRttUpdated(uint64_t rtt) override;
    size_t GetCongestionWindow() const override;
    size_t GetBytesInFlight() const override;
    bool CanSend(uint64_t now, uint32_t& can_send_bytes) const override;
    uint64_t GetPacingRate() const override;
    void Reset() override;

    
    size_t GetInitialWindow() const { return INITIAL_WINDOW; }

private:
    // Reno specific parameters
    static constexpr size_t INITIAL_WINDOW = 10 * 1460; // Initial window size
    static constexpr double BETA_RENO = 0.5;            // Beta for multiplicative decrease
    static constexpr size_t MIN_WINDOW = 2 * 1460;      // Minimum window size
    
    // Reno state variables
    size_t ssthresh_;           // Slow start threshold
    uint64_t recovery_start_;   // Start time of recovery period
    bool in_recovery_;          // Whether in recovery period
};

}
}

#endif
