#include <algorithm>

#include "quic/congestion_control/util.h"
#include "quic/congestion_control/normal_pacer.h"
#include "quic/congestion_control/bbr_v2_congestion_control.h"

namespace quicx {
namespace quic {

static inline uint64_t MulDiv(uint64_t a, uint64_t num, uint64_t den) {
    return congestion_control::muldiv_safe(a, num, den);
}

BBRv2CongestionControl::BBRv2CongestionControl() {
    Configure({});
}

void BBRv2CongestionControl::Configure(const CcConfigV2& cfg) {
    cfg_ = cfg;
    cwnd_bytes_ = std::max<uint64_t>(cfg_.initial_cwnd_bytes, 4 * cfg_.mss_bytes);
    ssthresh_bytes_ = UINT64_MAX;
    bytes_in_flight_ = 0;

    srtt_us_ = 0;
    min_rtt_us_ = 0;
    min_rtt_stamp_us_ = 0;

    bw_window_.clear();
    max_bw_bps_ = 0;

    pacing_gain_ = 2.885; // STARTUP
    cwnd_gain_ = 2.0;
    cycle_index_ = 0;
    cycle_start_us_ = 0;
    probe_rtt_done_stamp_valid_ = false;
    probe_rtt_done_stamp_us_ = 0;

    inflight_hi_bytes_ = UINT64_MAX;
    inflight_lo_bytes_ = 0;
    loss_event_count_in_round_ = 0;
    
    // Initialize full bandwidth detection
    full_bw_bps_ = 0;
    full_bw_cnt_ = 0;

    bw_sample_start_us_ = 0;
    bw_sample_bytes_acc_ = 0;

    mode_ = Mode::kStartup;
    if (!pacer_) pacer_.reset(new NormalPacer());
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void BBRv2CongestionControl::OnPacketSent(const SentPacketEvent& ev) {
    bytes_in_flight_ += ev.bytes;
    if (pacer_) pacer_->OnPacketSent(ev.sent_time, static_cast<size_t>(ev.bytes));
}

void BBRv2CongestionControl::OnPacketAcked(const AckEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_acked) ? bytes_in_flight_ - ev.bytes_acked : 0;

    // ECN-CE: similar to BBRv3 simple reaction, slightly tighten inflight_hi
    if (ev.ecn_ce) {
        inflight_hi_bytes_ = std::max<uint64_t>(inflight_lo_bytes_, (inflight_hi_bytes_ * 95) / 100); // -5%
    }

    // Update min_rtt timestamp when min_rtt is updated (from OnRoundTripSample)
    if (min_rtt_stamp_us_ == 0 || srtt_us_ <= min_rtt_us_) {
        min_rtt_stamp_us_ = ev.ack_time;
    }

    // Aggregate bandwidth over at least one SRTT to avoid underestimation
    if (srtt_us_ > 0) {
        if (bw_sample_start_us_ == 0) {
            bw_sample_start_us_ = ev.ack_time;
            bw_sample_bytes_acc_ = ev.bytes_acked;
        } else {
            bw_sample_bytes_acc_ += ev.bytes_acked;
            uint64_t elapsed = ev.ack_time - bw_sample_start_us_;
            if (elapsed >= srtt_us_) {
                uint64_t bytes_per_sec = MulDiv(bw_sample_bytes_acc_, 1000000ull, elapsed);
                UpdateMaxBandwidth(bytes_per_sec, ev.ack_time);
                bw_sample_start_us_ = ev.ack_time;
                bw_sample_bytes_acc_ = 0;
            }
        }
    }

    // BBRv2: gradually increase inflight_hi when no loss
    // Note: inflight_hi is reduced in OnPacketLost with beta=0.7
    // We should NOT reduce it again here to avoid double penalization
    if (loss_event_count_in_round_ > 0) {
        // Reset counter but don't reduce inflight_hi (already done in OnPacketLost)
        loss_event_count_in_round_ = 0;
    } else if (max_bw_bps_ > 0) {
        // loosen hi by small step when no loss
        inflight_hi_bytes_ = std::min<uint64_t>(inflight_hi_bytes_ + cfg_.mss_bytes, cfg_.max_cwnd_bytes);
    }

    CheckStartupFullBandwidth(ev.ack_time);
    MaybeEnterOrExitProbeRtt(ev.ack_time);

    // Target cwnd = min(hi, gain*BDP). Do not reduce cwnd on ACK.
    uint64_t bdp = BdpBytes(static_cast<uint64_t>(cwnd_gain_ * 1000), 1000);
    uint64_t target = std::min<uint64_t>(bdp, inflight_hi_bytes_);
    if (cwnd_bytes_ < target) {
        cwnd_bytes_ += ev.bytes_acked;
        cwnd_bytes_ = std::min<uint64_t>(cwnd_bytes_, target);
        cwnd_bytes_ = std::min<uint64_t>(cwnd_bytes_, cfg_.max_cwnd_bytes);
    }

    if (mode_ == Mode::kProbeBw) {
        AdvanceProbeBwCycle(ev.ack_time);
    }
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void BBRv2CongestionControl::OnPacketLost(const LossEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_lost) ? bytes_in_flight_ - ev.bytes_lost : 0;
    loss_event_count_in_round_++;
    
    // BBRv2: reduce hi bound on loss using multiplicative decrease (beta = 0.7)
    // More conservative than /2, aligns with BBRv2 standard
    inflight_hi_bytes_ = std::max<uint64_t>(inflight_lo_bytes_, (inflight_hi_bytes_ * 7) / 10);
    
    if (mode_ == Mode::kStartup) {
        mode_ = Mode::kDrain;
        pacing_gain_ = 1.0 / 2.885;
        cwnd_gain_ = 2.0;
    }
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void BBRv2CongestionControl::OnRoundTripSample(uint64_t latest_rtt, uint64_t ack_delay) {
    (void)ack_delay;
    if (srtt_us_ == 0) srtt_us_ = latest_rtt;
    srtt_us_ = (7 * srtt_us_ + latest_rtt) / 8;
    // Note: min_rtt timestamp is updated in OnPacketAcked where ack_time is available
    if (min_rtt_us_ == 0 || latest_rtt < min_rtt_us_) {
        min_rtt_us_ = latest_rtt;
    }
}

ICongestionControl::SendState BBRv2CongestionControl::CanSend(uint64_t now, uint64_t& can_send_bytes) const {
    (void)now;
    uint64_t bdp = BdpBytes(static_cast<uint64_t>(cwnd_gain_ * 1000), 1000);
    uint64_t target = std::min<uint64_t>(bdp, inflight_hi_bytes_);
    uint64_t left = (target > bytes_in_flight_) ? (target - bytes_in_flight_) : 0;
    can_send_bytes = left;
    if (left == 0) return SendState::kBlockedByCwnd;
    return SendState::kOk;
}

uint64_t BBRv2CongestionControl::GetPacingRateBps() const {
    // max_bw_bps_ is in bytes/sec; return bytes/sec pacing rate
    if (max_bw_bps_ == 0) {
        uint64_t rtt_us = (srtt_us_ > 0) ? srtt_us_ : 333000;
        uint64_t bw_bytes_per_sec = MulDiv(cwnd_bytes_, 1000000ull, rtt_us);
        return static_cast<uint64_t>(bw_bytes_per_sec * pacing_gain_);
    }
    return static_cast<uint64_t>(max_bw_bps_ * pacing_gain_);
}

uint64_t BBRv2CongestionControl::NextSendTime(uint64_t now) const {
    (void)now;
    if (!pacer_) return 0;
    return pacer_->TimeUntilSend();
}

uint64_t BBRv2CongestionControl::BdpBytes(uint64_t gain_num, uint64_t gain_den) const {
    if (min_rtt_us_ == 0) {
        // Use initial cwnd instead of potentially large cwnd_bytes_
        uint64_t default_bdp = std::max<uint64_t>(cfg_.initial_cwnd_bytes, 4 * cfg_.mss_bytes);
        return congestion_control::muldiv_safe(default_bdp, gain_num, gain_den);
    }
    uint64_t bw = (max_bw_bps_ > 0) ? max_bw_bps_ : (srtt_us_ > 0 ? MulDiv(cwnd_bytes_, 1000000ull, srtt_us_) : cfg_.initial_cwnd_bytes);
    uint64_t bdp = MulDiv(bw, min_rtt_us_, 1000000ull); // bytes
    return congestion_control::muldiv_safe(bdp, gain_num, gain_den);
}

void BBRv2CongestionControl::MaybeEnterOrExitProbeRtt(uint64_t now_us) {
    // Enter ProbeRTT if min_rtt hasn't been updated in 10 seconds
    // BBR standard: probe RTT when min_rtt is stale (> 10s old)
    if (mode_ != Mode::kProbeRtt && 
        min_rtt_stamp_us_ > 0 && 
        now_us - min_rtt_stamp_us_ >= kProbeRttIntervalUs) {
        mode_ = Mode::kProbeRtt;
        pacing_gain_ = 1.0;
        // BBR standard: reduce cwnd to 4*MSS to drain queue and get accurate RTT
        cwnd_bytes_ = std::max<uint64_t>(4 * cfg_.mss_bytes, cfg_.min_cwnd_bytes);
        probe_rtt_done_stamp_valid_ = true;
        probe_rtt_done_stamp_us_ = now_us + kProbeRttTimeUs;
    }
    
    // Exit ProbeRTT after 200ms
    if (mode_ == Mode::kProbeRtt && now_us >= probe_rtt_done_stamp_us_) {
        // min_rtt should have been updated during ProbeRTT
        min_rtt_stamp_us_ = now_us;  // Reset timestamp
        mode_ = Mode::kProbeBw;
        pacing_gain_ = 1.0;
        cwnd_gain_ = 2.0;
        // Start so that the first cycle advance goes to index 0 (gain 1.25)
        cycle_index_ = 7;
        cycle_start_us_ = now_us;
    }
}

void BBRv2CongestionControl::AdvanceProbeBwCycle(uint64_t now_us) {
    static const double kGainCycle[8] = {1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    uint64_t cycle_len_us = std::max<uint64_t>(min_rtt_us_, 1000);
    if (now_us - cycle_start_us_ >= cycle_len_us) {
        cycle_index_ = (cycle_index_ + 1) % 8;
        cycle_start_us_ = now_us;
        pacing_gain_ = kGainCycle[cycle_index_];
    }
}

void BBRv2CongestionControl::UpdateMaxBandwidth(uint64_t sample_bps, uint64_t now_us) {
    bw_window_.push_back({now_us, sample_bps});
    // Use deque::pop_front() for O(1) removal instead of vector::erase(begin()) which is O(n)
    while (bw_window_.size() > kBwWindow) {
        bw_window_.pop_front();
    }
    uint64_t m = 0;
    for (const auto& s : bw_window_) {
        m = std::max<uint64_t>(m, s.bytes_per_sec);
    }
    max_bw_bps_ = m;
}

void BBRv2CongestionControl::CheckStartupFullBandwidth(uint64_t now_us) {
    (void)now_us;
    if (max_bw_bps_ == 0) return;
    
    // Check if bandwidth is still growing (>25% increase)
    if (full_bw_bps_ == 0 || max_bw_bps_ > full_bw_bps_ * 125 / 100) {
        full_bw_bps_ = max_bw_bps_;  // Update full bandwidth
        full_bw_cnt_ = 0;             // Reset counter
    } else {
        full_bw_cnt_++;
        // Exit STARTUP if bandwidth stopped growing for 3 rounds
        if (mode_ == Mode::kStartup && full_bw_cnt_ >= 3) {
            mode_ = Mode::kDrain;
            pacing_gain_ = 1.0 / 2.885;
            cwnd_gain_ = 2.0;
        }
    }
    if (mode_ == Mode::kDrain) {
        uint64_t bdp = BdpBytes(1000, 1000);
        if (bytes_in_flight_ <= bdp) {
            mode_ = Mode::kProbeBw;
            pacing_gain_ = 1.0;
            cwnd_gain_ = 2.0;
            cycle_index_ = 0;
            cycle_start_us_ = now_us;
        } else if (min_rtt_us_ == 0) {
            // If min_rtt is not yet sampled, exit drain immediately to avoid blocking
            mode_ = Mode::kProbeBw;
            pacing_gain_ = 1.0;
            cwnd_gain_ = 2.0;
            cycle_index_ = 0;
            cycle_start_us_ = now_us;
        }
    }
}

} // namespace quic
} // namespace quicx


