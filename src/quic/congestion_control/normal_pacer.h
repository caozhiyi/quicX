#ifndef QUIC_CONGESTION_CONTROL_NORMAL_PACER
#define QUIC_CONGESTION_CONTROL_NORMAL_PACER

#include "quic/congestion_control/if_pacer.h"

namespace quicx {
namespace quic {

class NormalPacer:
    public IPacer {
public:
    NormalPacer();
    ~NormalPacer() override;

    void OnPacingRateUpdated(uint64_t pacing_rate) override;

    bool CanSend(uint64_t now) const override;

    uint64_t TimeUntilSend() const override;

    void OnPacketSent(uint64_t sent_time, uint64_t bytes) override;

    void Reset() override;

private:
    void RefillBurstBudget(uint64_t now_ms);

private:
    // Pacing configuration/state
    uint64_t pacing_rate_bytes_per_sec_; // bytes per second
    uint64_t next_send_time_ms_;         // absolute time in ms when next send is allowed
    uint64_t last_update_ms_;            // last time we refilled burst budget

    // Simple burst budget to allow small bursts without delay
    uint64_t max_burst_bytes_;
    uint64_t burst_budget_bytes_;
};

}
}

#endif
