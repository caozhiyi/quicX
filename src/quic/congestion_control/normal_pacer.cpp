#include <quicx/common/metrics.h>

#include "common/metrics/metrics_std.h"
#include "common/util/time.h"

#include "quic/congestion_control/normal_pacer.h"

namespace quicx {
namespace quic {

NormalPacer::NormalPacer() {
    pacing_rate_bytes_per_sec_ = 0;
    next_send_time_us_ = 0;
    last_update_us_ = 0;
    max_burst_bytes_ = 256 * 1024;  // 256KB default burst (was 16KB; small burst severely limited LAN throughput)
    burst_budget_bytes_ = max_burst_bytes_;
}

NormalPacer::~NormalPacer() {}

void NormalPacer::OnPacingRateUpdated(uint64_t pacing_rate) {
    pacing_rate_bytes_per_sec_ = pacing_rate;
}

bool NormalPacer::CanSend(uint64_t now_us) const {
    if (pacing_rate_bytes_per_sec_ == 0) {
        return true;
    }
    if (burst_budget_bytes_ > 0) {
        return true;
    }
    return now_us >= next_send_time_us_;
}

uint64_t NormalPacer::TimeUntilSend() const {
    if (pacing_rate_bytes_per_sec_ == 0) {
        return 0;
    }
    uint64_t now_us = Metrics::NowUs();
    if (now_us >= next_send_time_us_) {
        return 0;
    }
    uint64_t delay_us = next_send_time_us_ - now_us;

    // Metrics: Record pacing delay in microseconds
    Metrics::GaugeSet(common::MetricsStd::PacingDelayUs, delay_us);

    return delay_us;
}

void NormalPacer::OnPacketSent(uint64_t sent_time_us, uint64_t bytes) {
    RefillBurstBudget(sent_time_us);

    if (burst_budget_bytes_ >= bytes) {
        burst_budget_bytes_ -= bytes;
        next_send_time_us_ = sent_time_us;  // still can send immediately within burst budget
        return;
    }

    // No burst budget: schedule next send by pacing rate
    burst_budget_bytes_ = 0;
    if (pacing_rate_bytes_per_sec_ > 0) {
        // time delta in us to transmit 'bytes' at pacing rate; keeps sub-ms
        // pacing gaps (high rates / small packets) intact.
        uint64_t us = (bytes * 1000000ull + pacing_rate_bytes_per_sec_ - 1) / pacing_rate_bytes_per_sec_;
        next_send_time_us_ = sent_time_us + us;
    } else {
        next_send_time_us_ = sent_time_us;
    }
}

void NormalPacer::Reset() {
    pacing_rate_bytes_per_sec_ = 0;
    next_send_time_us_ = 0;
    last_update_us_ = 0;
    burst_budget_bytes_ = max_burst_bytes_;
}

void NormalPacer::RefillBurstBudget(uint64_t now_us) {
    if (last_update_us_ == 0) {
        last_update_us_ = now_us;
        return;
    }
    if (pacing_rate_bytes_per_sec_ == 0) {
        burst_budget_bytes_ = max_burst_bytes_;
        last_update_us_ = now_us;
        return;
    }

    uint64_t elapsed_us = now_us > last_update_us_ ? now_us - last_update_us_ : 0;
    if (elapsed_us == 0) {
        return;
    }

    // Refill proportional to elapsed time and pacing rate, bounded by max burst
    uint64_t refill = (pacing_rate_bytes_per_sec_ * elapsed_us) / 1000000ull;
    burst_budget_bytes_ = std::min<uint64_t>(max_burst_bytes_, burst_budget_bytes_ + refill);
    last_update_us_ = now_us;
}

}  // namespace quic
}  // namespace quicx
