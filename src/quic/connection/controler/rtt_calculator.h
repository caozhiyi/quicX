#ifndef QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR
#define QUIC_CONNECTION_CONTROLER_RTT_CALCULATOR

#include <cstdint>

namespace quicx {
namespace quic {

const static uint32_t kInitRtt = 250;

class RttCalculator {
public:
    RttCalculator();
    ~RttCalculator();

    bool UpdateRtt(uint64_t send_time, uint64_t now, uint64_t ack_delay);

    void Reset();

    uint32_t GetPT0Interval(uint32_t max_ack_delay);

    uint32_t GetLatestRtt() { return latest_rtt_; }
    uint32_t GetRttVar() { return rtt_var_; }
    uint32_t GetMinRtt() { return min_rtt_; }
    uint32_t GetSmoothedRtt() { return smoothed_rtt_; }

    // RFC 9002 Section 6.2: PTO with exponential backoff
    uint32_t GetPTOWithBackoff(uint32_t max_ack_delay);
    void OnPTOExpired();
    void OnPacketAcked();
    uint32_t GetConsecutivePTOCount() const { return consecutive_pto_count_; }

    static constexpr uint32_t kMaxPTOBackoff = 6;        // Max 2^6 = 64x backoff
    static constexpr uint32_t kMaxConsecutivePTOs = 16;  // ~3 PTO cycles for idle timeout

private:
    uint32_t latest_rtt_;
    uint32_t rtt_var_;
    uint32_t min_rtt_;
    uint32_t smoothed_rtt_;

    uint64_t last_update_time_;

    // RFC 9002: PTO backoff state
    uint32_t pto_count_ = 0;              // Current backoff exponent
    uint32_t consecutive_pto_count_ = 0;  // Count of consecutive PTOs without ACK
};

}  // namespace quic
}  // namespace quicx

#endif