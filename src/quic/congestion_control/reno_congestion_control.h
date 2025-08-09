#ifndef QUIC_CONGESTION_CONTROL_RENO_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_RENO_CONGESTION_CONTROL

#include "quic/congestion_control/if_congestion_control.h"
#include "quic/congestion_control/if_pacer.h"
#include <memory>

namespace quicx {
namespace quic {


class RenoCongestionControl:
    public ICongestionControl {
public:
    RenoCongestionControl();
    ~RenoCongestionControl() override = default;

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

    bool InSlowStart() const override { return in_slow_start_; }
    bool InRecovery() const override { return in_recovery_; }
    uint64_t GetSsthresh() const override { return ssthresh_bytes_; }

private:
    void IncreaseOnAck(uint64_t bytes_acked);
    void EnterRecovery(uint64_t now);
    void UpdatePacingRate();

    // Config
    CcConfigV2 cfg_{};

    // State
    uint64_t cwnd_bytes_ = 0;
    uint64_t bytes_in_flight_ = 0;
    uint64_t ssthresh_bytes_ = UINT64_MAX;
    bool in_slow_start_ = true;
    bool in_recovery_ = false;
    uint64_t recovery_start_time_ = 0;
    uint64_t srtt_us_ = 0;

    // Pacer
    std::unique_ptr<IPacer> pacer_;
};

} // namespace quic
} // namespace quicx

#endif