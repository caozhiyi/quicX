#ifndef QUIC_CONGESTION_CONTROL_IF_PACER
#define QUIC_CONGESTION_CONTROL_IF_PACER

#include <cstdint>

namespace quicx {
namespace quic {

// Interface for pacing packet sending
class IPacer {
public:
    IPacer() {}
    virtual ~IPacer() {}

    // Called when pacing rate changes
    virtual void OnPacingRateUpdated(uint64_t pacing_rate) = 0;

    // Check if can send packet now
    virtual bool CanSend(uint64_t now) const = 0;

    // Get time of next send
    virtual uint64_t TimeUntilSend() const = 0;

    // Called when packet is sent
    virtual void OnPacketSent(uint64_t sent_time, size_t bytes) = 0;

    // Reset pacer state
    virtual void Reset() = 0;
};

}
}

#endif
