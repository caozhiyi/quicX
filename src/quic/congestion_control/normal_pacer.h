#ifndef QUIC_CONGESTION_CONTROL_NORMAL_PACER
#define QUIC_CONGESTION_CONTROL_NORMAL_PACER

#include "quic/congestion_control/if_pacer.h"

namespace quicx {
namespace quic {

class NormalPacer:
    public IPacer {
public:
    NormalPacer();
    virtual ~NormalPacer();

    void OnPacingRateUpdated(uint64_t pacing_rate) override;

    bool CanSend(uint64_t now) const override;

    uint64_t TimeUntilSend() const override;

    void OnPacketSent(uint64_t sent_time, size_t bytes) override;

    void Reset() override;

private:
    void ReplenishTokens();

private:
    size_t burst_tokens_;     // Available burst allowance in bytes
    size_t max_burst_size_;   // Maximum allowed burst size
    size_t max_burst_tokens_; // Maximum number of burst tokens allowed
    size_t bytes_in_flight_;  // Bytes currently in flight

    uint64_t last_replenish_time_; // Last time tokens were replenished
    uint64_t pacing_interval_;     // Interval for pacing
    uint64_t pacing_rate_;         // Current pacing rate in bytes per second
    uint64_t last_send_time_;      // Time of last packet sent
};

}
}

#endif
