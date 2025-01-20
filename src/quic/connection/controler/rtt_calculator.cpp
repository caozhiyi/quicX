#include <limits>
#include <cstdint>
#include <algorithm>
#include "common/log/log.h"
#include "quic/connection/controler/rtt_calculator.h"

namespace quicx {
namespace quic {

RttCalculator::RttCalculator() {
    Reset();
}

RttCalculator::~RttCalculator() {

}

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
        // MUST use the smaller of the acknowledgment delay and the peer's max_ack_delay after the handshake is confirmed

        uint32_t adjusted_rtt = latest_rtt_;
        if (latest_rtt_ >= (min_rtt_ + ack_delay)) {
            adjusted_rtt -= ack_delay;
        }

        // smoothed_rtt = 7/8 * smoothed_rtt + 1/8 * adjusted_rtt
        smoothed_rtt_ = smoothed_rtt_ - smoothed_rtt_ >> 3 + adjusted_rtt >> 3;

        // rttvar_sample = abs(smoothed_rtt - adjusted_rtt)
        uint32_t rttvar_sample = smoothed_rtt_ > adjusted_rtt ? smoothed_rtt_ - adjusted_rtt : adjusted_rtt - smoothed_rtt_;

        // rttvar = 3/4 * rttvar + 1/4 * rttvar_sample
        rtt_var_ = rtt_var_ - rtt_var_ >> 2 + rttvar_sample >> 2;
    }
    last_update_time_ = now;

    return true;
}

void RttCalculator::Reset() {
    latest_rtt_ = 0;
    smoothed_rtt_ = __init_rtt * 1000;
    rtt_var_ = smoothed_rtt_ / 2;
    min_rtt_ = std::numeric_limits<uint32_t>::max();

    last_update_time_ = 0;
}

uint32_t RttCalculator::GetPT0Interval(uint32_t max_ack_delay) {
    // PTO = smoothed_rtt + max(4*rttvar, kGranularity) + max_ack_delay
    return smoothed_rtt_ + std::max<uint32_t>(rtt_var_ << 2, 1000) + max_ack_delay;
}

}
}
