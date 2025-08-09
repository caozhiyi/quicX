#ifndef QUIC_CONGESTION_CONTROL_IF_CONGESTION_CONTROL
#define QUIC_CONGESTION_CONTROL_IF_CONGESTION_CONTROL

#include <cstdint>

namespace quicx {
namespace quic {

struct CcConfigV2 {
    uint64_t initial_cwnd_bytes = 10 * 1460;
    uint64_t min_cwnd_bytes = 2 * 1460;
    uint64_t max_cwnd_bytes = 1000 * 1460;
    uint64_t mss_bytes = 1460;
    double beta = 0.5;        // cwnd *= beta on loss
    bool ecn_enabled = false; // reserved
};

struct SentPacketEvent {
    uint64_t pn = 0;
    uint64_t bytes = 0;
    uint64_t sent_time = 0;
    bool is_retransmit = false;
};

struct AckEvent {
    uint64_t pn = 0;
    uint64_t bytes_acked = 0;
    uint64_t ack_time = 0;
    uint64_t ack_delay = 0;
    bool ecn_ce = false;
};

struct LossEvent {
    uint64_t pn = 0;
    uint64_t bytes_lost = 0;
    uint64_t lost_time = 0;
};

class ICongestionControl {
public:
    virtual ~ICongestionControl() = default;

    virtual void Configure(const CcConfigV2& cfg) = 0;

    virtual void OnPacketSent(const SentPacketEvent& ev) = 0;
    virtual void OnPacketAcked(const AckEvent& ev) = 0;
    virtual void OnPacketLost(const LossEvent& ev) = 0;
    virtual void OnRoundTripSample(uint64_t latest_rtt, uint64_t ack_delay = 0) = 0;

    enum class SendState { kOk, kBlockedByCwnd, kBlockedByPacing };
    virtual SendState CanSend(uint64_t now, uint64_t& can_send_bytes) const = 0;

    virtual uint64_t GetCongestionWindow() const = 0;
    virtual uint64_t GetBytesInFlight() const = 0;
    virtual uint64_t GetPacingRateBps() const = 0;
    virtual uint64_t NextSendTime(uint64_t now) const = 0;

    // Observability helpers
    virtual bool InSlowStart() const = 0;
    virtual bool InRecovery() const = 0;
    virtual uint64_t GetSsthresh() const = 0;
};

} // namespace quic
} // namespace quicx

#endif