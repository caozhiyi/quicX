#include "common/util/time.h"
#include "quic/congestion_control/normal_pacer.h"

namespace quicx {
namespace quic {

NormalPacer::NormalPacer() {
    pacing_rate_bytes_per_sec_ = 0;
    next_send_time_ms_ = 0;
    last_update_ms_ = 0;
    max_burst_bytes_ = 16 * 1024; // 16KB default burst
    burst_budget_bytes_ = max_burst_bytes_;
}

NormalPacer::~NormalPacer() {}

void NormalPacer::OnPacingRateUpdated(uint64_t pacing_rate) {
    pacing_rate_bytes_per_sec_ = pacing_rate;
}

bool NormalPacer::CanSend(uint64_t now_ms) const {
    if (pacing_rate_bytes_per_sec_ == 0) {
        return true;
    }
    if (burst_budget_bytes_ > 0) {
        return true;
    }
    return now_ms >= next_send_time_ms_;
}

uint64_t NormalPacer::TimeUntilSend() const {
    if (pacing_rate_bytes_per_sec_ == 0) {
        return 0;
    }
    uint64_t now_ms = common::UTCTimeMsec();
    if (now_ms >= next_send_time_ms_) {
        return 0;
    }
    return next_send_time_ms_ - now_ms;
}

void NormalPacer::OnPacketSent(uint64_t sent_time_ms, uint64_t bytes) {
    RefillBurstBudget(sent_time_ms);

    if (burst_budget_bytes_ >= bytes) {
        burst_budget_bytes_ -= bytes;
        next_send_time_ms_ = sent_time_ms; // still can send immediately within burst budget
        return;
    }

    // No burst budget: schedule next send by pacing rate
    burst_budget_bytes_ = 0;
    if (pacing_rate_bytes_per_sec_ > 0) {
        // time delta in ms to transmit 'bytes' at pacing rate
        uint64_t ms = (bytes * 1000ull + pacing_rate_bytes_per_sec_ - 1) / pacing_rate_bytes_per_sec_;
        next_send_time_ms_ = sent_time_ms + ms;
    } else {
        next_send_time_ms_ = sent_time_ms;
    }
}

void NormalPacer::Reset() {
    pacing_rate_bytes_per_sec_ = 0;
    next_send_time_ms_ = 0;
    last_update_ms_ = 0;
    burst_budget_bytes_ = max_burst_bytes_;
}

void NormalPacer::RefillBurstBudget(uint64_t now_ms) {
    if (last_update_ms_ == 0) {
        last_update_ms_ = now_ms;
        return;
    }
    if (pacing_rate_bytes_per_sec_ == 0) {
        burst_budget_bytes_ = max_burst_bytes_;
        last_update_ms_ = now_ms;
        return;
    }

    uint64_t elapsed_ms = now_ms - last_update_ms_;
    if (elapsed_ms == 0) {
        return;
    }

    // Refill proportional to elapsed time and pacing rate, bounded by max burst
    uint64_t refill = (pacing_rate_bytes_per_sec_ * elapsed_ms) / 1000ull;
    burst_budget_bytes_ = std::min<uint64_t>(max_burst_bytes_, burst_budget_bytes_ + refill);
    last_update_ms_ = now_ms;
}

} // namespace quic
} // namespace quicx
