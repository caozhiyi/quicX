#ifndef QUIC_CONGESTION_CONTROL_IF_PACER
#define QUIC_CONGESTION_CONTROL_IF_PACER

#include <cstdint>

namespace quicx {
namespace quic {

class IPacer {
public:
    virtual ~IPacer() = default;

    virtual void OnPacingRateUpdated(uint64_t pacing_rate) = 0;

    virtual bool CanSend(uint64_t now) const = 0;

    virtual uint64_t TimeUntilSend() const = 0;

    virtual void OnPacketSent(uint64_t sent_time, uint64_t bytes) = 0;

    virtual void Reset() = 0;
};

}
}

#endif
