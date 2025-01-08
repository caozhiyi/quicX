#ifndef QUIC_CONGESTION_CONTROL_IF_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_IF_CONGESTION_CONTROL

#include <memory>
#include <cstdint>

namespace quicx {
namespace quic {

class ICongestionControl {
public:
    ICongestionControl() {}
    virtual ~ICongestionControl() {}

    // called when a packet is sent
    virtual void OnPacketSent(size_t bytes, uint64_t sent_time) = 0;

    // called when a packet is acked
    virtual void OnPacketAcked(size_t bytes, uint64_t ack_time) = 0;

    // called when a packet is lost
    virtual void OnPacketLost(size_t bytes, uint64_t lost_time) = 0;

    virtual void OnRttUpdated(uint64_t rtt) = 0;

    // get current congestion window size
    virtual size_t GetCongestionWindow() const = 0;

    // get current bytes allowed to send
    virtual size_t GetBytesInFlight() const = 0;

    // check if can send new packet
    virtual bool CanSend(size_t bytes_in_flight) const = 0;

    // get current send rate
    virtual uint64_t GetPacingRate() const = 0;

    // reset congestion control state
    virtual void Reset() = 0;

protected:
    // base state
    size_t congestion_window_; // congestion window size
    size_t bytes_in_flight_;   // bytes in flight
    bool in_slow_start_;       // in slow start
    uint64_t pacing_rate_;     // send rate

    // rtt
    uint64_t min_rtt_;
    uint64_t smoothed_rtt_;
    uint64_t latest_rtt_;
    uint64_t rtt_variance_;

    // config
    size_t max_congestion_window_;
    size_t min_congestion_window_;
    size_t initial_congestion_window_;
};

}
}

#endif
