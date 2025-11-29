#include <algorithm>
#include <cstdint>
#include <limits>

#include "common/log/log.h"

#include "quic/connection/controler/rtt_calculator.h"

namespace quicx {
namespace quic {

RttCalculator::RttCalculator() {
    Reset();
}

RttCalculator::~RttCalculator() {}

bool RttCalculator::UpdateRtt(uint64_t send_time, uint64_t now, uint64_t ack_delay) {
    common::LOG_DEBUG("update rtt. send time:%lld, now:%lld, ack delay:%d", send_time, now, ack_delay);

    latest_rtt_ = now - send_time;
    // first update rtt
    if (last_update_time_ == 0) {
        min_rtt_ = latest_rtt_;
        smoothed_rtt_ = latest_rtt_;
        rtt_var_ = latest_rtt_ >> 1;

    } else {
        min_rtt_ = std::min(min_rtt_, latest_rtt_);

        // TODO:
        // SHOULD ignore the peer's max_ack_delay until the handshake is confirmed
        // MUST use the smaller of the acknowledgment delay and the peer's max_ack_delay after the handshake is
        // confirmed

        uint32_t adjusted_rtt = latest_rtt_;
        if (latest_rtt_ >= (min_rtt_ + ack_delay)) {
            adjusted_rtt -= ack_delay;
        }

        // smoothed_rtt = 7/8 * smoothed_rtt + 1/8 * adjusted_rtt
        smoothed_rtt_ = smoothed_rtt_ - (smoothed_rtt_ >> 3) + (adjusted_rtt >> 3);

        // rttvar_sample = abs(smoothed_rtt - adjusted_rtt)
        uint32_t rttvar_sample =
            smoothed_rtt_ > adjusted_rtt ? smoothed_rtt_ - adjusted_rtt : adjusted_rtt - smoothed_rtt_;

        // rttvar = 3/4 * rttvar + 1/4 * rttvar_sample
        rtt_var_ = rtt_var_ - (rtt_var_ >> 2) + (rttvar_sample >> 2);
    }
    last_update_time_ = now;

    return true;
}

void RttCalculator::Reset() {
    latest_rtt_ = 0;
    smoothed_rtt_ = kInitRtt;  // kInitRtt is 250ms, no need to multiply by 1000
    rtt_var_ = smoothed_rtt_ / 2;
    min_rtt_ = std::numeric_limits<uint32_t>::max();

    last_update_time_ = 0;
}

uint32_t RttCalculator::GetPT0Interval(uint32_t max_ack_delay) {
    // PTO = smoothed_rtt + max(4*rttvar, kGranularity) + max_ack_delay
    // kGranularity is 1ms, so use 1 instead of 1000
    return smoothed_rtt_ + std::max<uint32_t>(rtt_var_ << 2, 1) + max_ack_delay;
}

// RFC 9002 Section 6.2: PTO with exponential backoff
uint32_t RttCalculator::GetPTOWithBackoff(uint32_t max_ack_delay) {
    uint32_t base_pto = GetPT0Interval(max_ack_delay);

    // Apply exponential backoff: PTO * (2 ^ pto_count)
    // Limit backoff exponent to kMaxPTOBackoff (64x max)
    uint32_t backoff_exp = std::min(pto_count_, kMaxPTOBackoff);
    return base_pto << backoff_exp;  // Equivalent to base_pto * (2 ^ backoff_exp)
}

void RttCalculator::OnPTOExpired() {
    // Increment backoff for next PTO, capped at kMaxPTOBackoff
    pto_count_ = std::min(pto_count_ + 1, kMaxPTOBackoff);
    consecutive_pto_count_++;

    common::LOG_DEBUG("PTO expired: pto_count=%u, consecutive_pto_count=%u", pto_count_, consecutive_pto_count_);
}

void RttCalculator::OnPacketAcked() {
    // Reset backoff when we receive an ACK
    if (pto_count_ > 0 || consecutive_pto_count_ > 0) {
        common::LOG_DEBUG("Packet ACKed: resetting PTO backoff (was pto_count=%u, consecutive=%u)", pto_count_,
            consecutive_pto_count_);
    }
    pto_count_ = 0;
    consecutive_pto_count_ = 0;
}

}  // namespace quic
}  // namespace quicx
