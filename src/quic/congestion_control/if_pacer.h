#ifndef QUIC_CONGESTION_CONTROL_IF_PACER
#define QUIC_CONGESTION_CONTROL_IF_PACER

#include <cstdint>

namespace quicx {
namespace quic {

/**
 * @brief Pacer interface for controlling packet transmission rate
 *
 * Implements token bucket or similar algorithms to smooth packet sending.
 */
class IPacer {
public:
    virtual ~IPacer() = default;

    /**
     * @brief Update the pacing rate
     *
     * @param pacing_rate New pacing rate in bytes per second
     */
    virtual void OnPacingRateUpdated(uint64_t pacing_rate) = 0;

    /**
     * @brief Check if a packet can be sent now
     *
     * @param now Current time in microseconds
     * @return true if allowed to send, false otherwise
     */
    virtual bool CanSend(uint64_t now) const = 0;

    /**
     * @brief Get time until next send is allowed
     *
     * @return Microseconds until next send opportunity
     */
    virtual uint64_t TimeUntilSend() const = 0;

    /**
     * @brief Record a packet send event
     *
     * @param sent_time Time when packet was sent
     * @param bytes Number of bytes sent
     */
    virtual void OnPacketSent(uint64_t sent_time, uint64_t bytes) = 0;

    /**
     * @brief Reset the pacer state
     */
    virtual void Reset() = 0;
};

}
}

#endif
