#include "common/util/time.h"
#include "quic/congestion_control/normal_pacer.h"


namespace quicx {
namespace quic {

NormalPacer::NormalPacer() : 
    burst_tokens_(0),
    max_burst_size_(16 * 1024), // TODO: 16KB default max burst
    max_burst_tokens_(10),      // TODO: Example value for max burst tokens
    pacing_interval_(100),      // TODO: Default pacing interval in milliseconds
    last_replenish_time_(0) {
    pacing_rate_ = 0;
    last_send_time_ = 0;
    bytes_in_flight_ = 0;
}

NormalPacer::~NormalPacer() {

}

void NormalPacer::OnPacingRateUpdated(uint64_t pacing_rate) {
    pacing_rate_ = pacing_rate;
}

bool NormalPacer::CanSend(uint64_t now) const {
    if (pacing_rate_ == 0) {
        return true;
    }

    // Allow sending if we have burst tokens or enough time has passed
    if (burst_tokens_ > 0) {
        return true;
    }

    return TimeUntilSend() <= now;
}

uint64_t NormalPacer::TimeUntilSend() const {
    if (pacing_rate_ == 0 || last_send_time_ == 0) {
        return 0;
    }

    // Calculate time needed between packets based on pacing rate
    uint64_t delay = (1000 * bytes_in_flight_) / pacing_rate_; // Convert to milliseconds
    uint64_t next_send_time = last_send_time_ + delay;
    uint64_t now = common::UTCTimeMsec();

    if (next_send_time <= now) {
        return 0;
    }
    return next_send_time - now;
}

void NormalPacer::OnPacketSent(uint64_t sent_time, size_t bytes) {
    last_send_time_ = sent_time;
    bytes_in_flight_ = bytes;

    // Consume burst tokens if available
    if (burst_tokens_ >= bytes) {
        burst_tokens_ -= bytes;
    } else {
        burst_tokens_ = 0;
    }
    ReplenishTokens();
}

void NormalPacer::Reset() {
    pacing_rate_ = 0;
    last_send_time_ = 0;
    bytes_in_flight_ = 0;
    burst_tokens_ = max_burst_size_;
}

void NormalPacer::ReplenishTokens() {
    uint64_t current_time = common::UTCTimeMsec();
    if (current_time - last_replenish_time_ >= pacing_interval_) {
        burst_tokens_ = std::min(burst_tokens_ + 1, max_burst_tokens_);
        last_replenish_time_ = current_time;
    }
}

}
}
