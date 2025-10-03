#include <algorithm>

#include "quic/congestion_control/util.h"
#include "quic/congestion_control/normal_pacer.h"
#include "quic/congestion_control/bbr_v3_congestion_control.h"

namespace quicx {
namespace quic {

static inline uint64_t MulDiv(uint64_t a, uint64_t num, uint64_t den) {
    return congestion_control::muldiv_safe(a, num, den);
}

BBRv3CongestionControl::BBRv3CongestionControl() {
    Configure({});
}

void BBRv3CongestionControl::Configure(const CcConfigV2& cfg) {
    cfg_ = cfg;
    cwnd_bytes_ = std::max<uint64_t>(cfg_.initial_cwnd_bytes, 4 * cfg_.mss_bytes);
    ssthresh_bytes_ = UINT64_MAX;
    bytes_in_flight_ = 0;

    srtt_us_ = 0;
    min_rtt_us_ = 0;
    min_rtt_stamp_us_ = 0;

    bw_window_.clear();
    max_bw_bps_ = 0;
    bw_sample_start_us_ = 0;
    bw_sample_bytes_acc_ = 0;

    pacing_gain_ = 2.885;
    cwnd_gain_ = 2.0;
    cycle_index_ = 0;
    cycle_start_us_ = 0;
    probe_bw_state_ = ProbeBwState::kDown;
    probe_bw_state_start_us_ = 0;
    rounds_since_probe_ = 0;
    probe_rtt_done_stamp_valid_ = false;
    probe_rtt_done_stamp_us_ = 0;

    inflight_hi_bytes_ = UINT64_MAX;
    inflight_lo_bytes_ = 0;
    end_of_round_pn_ = 0;
    round_delivered_bytes_ = 0;
    round_lost_bytes_ = 0;
    loss_thresh_ = 0.02;
    beta_loss_ = 0.9;
    beta_ecn_ = 0.85;
    ecn_seen_in_round_ = false;

    full_bw_bps_ = 0;
    full_bw_cnt_ = 0;
    round_count_ = 0;
    round_start_pn_ = 0;

    mode_ = Mode::kStartup;
    if (!pacer_) pacer_.reset(new NormalPacer());
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void BBRv3CongestionControl::OnPacketSent(const SentPacketEvent& ev) {
    bytes_in_flight_ += ev.bytes;
    if (pacer_) pacer_->OnPacketSent(ev.sent_time, static_cast<size_t>(ev.bytes));
    if (end_of_round_pn_ == 0 || ev.pn > end_of_round_pn_) end_of_round_pn_ = ev.pn;
}

void BBRv3CongestionControl::OnPacketAcked(const AckEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_acked) ? bytes_in_flight_ - ev.bytes_acked : 0;
    
    // Update min_rtt timestamp when min_rtt is updated (from OnRoundTripSample)
    if (min_rtt_stamp_us_ == 0 || srtt_us_ <= min_rtt_us_) {
        min_rtt_stamp_us_ = ev.ack_time;
    }
    
    if (ev.ecn_ce) {
        ecn_seen_in_round_ = true;
        // ECN handling will be done at round end via AdaptOnEcn()
    }

    // Detect round boundary: when ACKs pass end_of_round_pn_
    if (end_of_round_pn_ > 0 && ev.pn >= end_of_round_pn_) {
        // Call loss and ECN adaptation at round end with complete data
        AdaptInflightBoundsOnLoss(ev.ack_time);
        AdaptOnEcn();
        UpdateInflightBounds();
        StartNewRound(ev.pn);
    }

    // per-round accounting
    round_delivered_bytes_ += ev.bytes_acked;

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

    // Startup/Drain transitions
    CheckStartupFullBandwidth(ev.ack_time);
    MaybeEnterOrExitProbeRtt(ev.ack_time);

    // cwnd target with hi/lo bounds
    uint64_t bdp = BdpBytes(static_cast<uint64_t>(cwnd_gain_ * 1000), 1000);
    uint64_t target = std::min<uint64_t>(bdp, inflight_hi_bytes_);
    target = std::max<uint64_t>(target, inflight_lo_bytes_);
    if (cwnd_bytes_ < target) {
        cwnd_bytes_ += ev.bytes_acked;
        if (cwnd_bytes_ > target) cwnd_bytes_ = target;
    }
    if (cwnd_bytes_ > cfg_.max_cwnd_bytes) cwnd_bytes_ = cfg_.max_cwnd_bytes;

    if (mode_ == Mode::kProbeBw) {
        AdvanceProbeBwCycle(ev.ack_time);
    }
    UpdatePacingRate();
}

void BBRv3CongestionControl::OnPacketLost(const LossEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_lost) ? bytes_in_flight_ - ev.bytes_lost : 0;
    round_lost_bytes_ += ev.bytes_lost;
    // Don't call AdaptInflightBoundsOnLoss here - it's called at round end for accuracy
    if (mode_ == Mode::kStartup) {
        mode_ = Mode::kDrain;
        pacing_gain_ = 1.0 / 2.885;
        cwnd_gain_ = 2.0;
        // Also exit Probe UP if in that state
        if (probe_bw_state_ == ProbeBwState::kUp) {
            EnterProbeBwState(ProbeBwState::kDown, ev.lost_time);
        }
    }
    UpdatePacingRate();
}

void BBRv3CongestionControl::OnRoundTripSample(uint64_t latest_rtt, uint64_t ack_delay) {
    (void)ack_delay;
    if (srtt_us_ == 0) srtt_us_ = latest_rtt;
    srtt_us_ = (7 * srtt_us_ + latest_rtt) / 8;
    if (min_rtt_us_ == 0 || latest_rtt < min_rtt_us_) {
        min_rtt_us_ = latest_rtt;
        // min_rtt_stamp_us_ is updated in OnPacketAcked where ack_time is available
    }
}

ICongestionControl::SendState BBRv3CongestionControl::CanSend(uint64_t now, uint64_t& can_send_bytes) const {
    (void)now;
    uint64_t bdp = BdpBytes(static_cast<uint64_t>(cwnd_gain_ * 1000), 1000);
    uint64_t target = std::min<uint64_t>(bdp, inflight_hi_bytes_);
    target = std::max<uint64_t>(target, inflight_lo_bytes_);
    uint64_t left = (target > bytes_in_flight_) ? (target - bytes_in_flight_) : 0;
    can_send_bytes = left;
    if (left == 0) return SendState::kBlockedByCwnd;
    return SendState::kOk;
}

uint64_t BBRv3CongestionControl::GetPacingRateBps() const {
    if (max_bw_bps_ == 0) {
        // Use default RTT instead of returning cwnd_bytes (which is not a rate)
        uint64_t rtt_us = (srtt_us_ > 0) ? srtt_us_ : 333000; // default 333ms
        uint64_t bw_bytes_per_sec = MulDiv(cwnd_bytes_, 1000000ull, rtt_us);
        return static_cast<uint64_t>(bw_bytes_per_sec * pacing_gain_);
    }
    return static_cast<uint64_t>(max_bw_bps_ * pacing_gain_);
}

uint64_t BBRv3CongestionControl::NextSendTime(uint64_t now) const {
    (void)now;
    if (!pacer_) return 0;
    return pacer_->TimeUntilSend();
}

uint64_t BBRv3CongestionControl::BdpBytes(uint64_t gain_num, uint64_t gain_den) const {
    if (min_rtt_us_ == 0) return std::max<uint64_t>(cwnd_bytes_, 4 * cfg_.mss_bytes);
    uint64_t bw = (max_bw_bps_ > 0) ? max_bw_bps_ : (srtt_us_ > 0 ? MulDiv(cwnd_bytes_, 1000000ull, srtt_us_) : cfg_.initial_cwnd_bytes);
    uint64_t bdp = MulDiv(bw, min_rtt_us_, 1000000ull); // bytes
    return congestion_control::muldiv_safe(bdp, gain_num, gain_den);
}

void BBRv3CongestionControl::MaybeEnterOrExitProbeRtt(uint64_t now_us) {
    // Enter ProbeRTT only if min_rtt is stale (>10s)
    if (mode_ != Mode::kProbeRtt &&
        min_rtt_stamp_us_ > 0 &&
        now_us - min_rtt_stamp_us_ >= kProbeRttIntervalUs) {
        mode_ = Mode::kProbeRtt;
        pacing_gain_ = 1.0;
        // reduce inflight to 4*MSS to measure RTT
        cwnd_bytes_ = std::max<uint64_t>(4 * cfg_.mss_bytes, cfg_.min_cwnd_bytes);
        probe_rtt_done_stamp_valid_ = true;
        probe_rtt_done_stamp_us_ = now_us + kProbeRttTimeUs;
    }
    if (mode_ == Mode::kProbeRtt && now_us >= probe_rtt_done_stamp_us_) {
        mode_ = Mode::kProbeBw;
        pacing_gain_ = 1.0;
        cwnd_gain_ = 2.0;
        // Enter ProbeBW in DOWN state to start
        EnterProbeBwState(ProbeBwState::kDown, now_us);
        cycle_index_ = 7;
        cycle_start_us_ = now_us;
        min_rtt_stamp_us_ = now_us; // reset staleness
    }
}

void BBRv3CongestionControl::AdvanceProbeBwCycle(uint64_t now_us) {
    // Use enhanced state machine if we have valid BW estimate
    if (max_bw_bps_ > 0 && min_rtt_us_ > 0 && ShouldAdvanceProbeBwState(now_us)) {
        switch (probe_bw_state_) {
            case ProbeBwState::kDown:
                EnterProbeBwState(ProbeBwState::kCruise, now_us);
                break;
            case ProbeBwState::kCruise:
                // Move to refill after waiting
                EnterProbeBwState(ProbeBwState::kRefill, now_us);
                break;
            case ProbeBwState::kRefill:
                EnterProbeBwState(ProbeBwState::kUp, now_us);
                break;
            case ProbeBwState::kUp:
                EnterProbeBwState(ProbeBwState::kDown, now_us);
                rounds_since_probe_ = 0;
                break;
        }
    } else {
        // Fallback to simple cycle for compatibility
        static const double kGainCycle[8] = {1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        uint64_t cycle_len_us = std::max<uint64_t>(min_rtt_us_, 1000);
        if (now_us - cycle_start_us_ >= cycle_len_us) {
            cycle_index_ = (cycle_index_ + 1) % 8;
            cycle_start_us_ = now_us;
            pacing_gain_ = kGainCycle[cycle_index_];
        }
    }
}

void BBRv3CongestionControl::UpdateMaxBandwidth(uint64_t sample_bps, uint64_t now_us) {
    bw_window_.push_back({now_us, sample_bps});
    // Use deque::pop_front() for O(1) removal instead of vector::erase(begin()) which is O(n)
    while (bw_window_.size() > kBwWindow) {
        bw_window_.pop_front();
    }
    uint64_t m = 0;
    for (const auto& s : bw_window_) m = std::max<uint64_t>(m, s.bytes_per_sec);
    max_bw_bps_ = m;
}

void BBRv3CongestionControl::CheckStartupFullBandwidth(uint64_t now_us) {
    (void)now_us;
    if (max_bw_bps_ == 0) return;
    if (full_bw_bps_ == 0 || max_bw_bps_ > full_bw_bps_ * 125 / 100) {
        full_bw_bps_ = max_bw_bps_;
        full_bw_cnt_ = 0;
    } else {
        full_bw_cnt_++;
        if (mode_ == Mode::kStartup && full_bw_cnt_ >= 3) {
            mode_ = Mode::kDrain;
            pacing_gain_ = 1.0 / 2.885;
            cwnd_gain_ = 2.0;
            // Set inflight_lo when exiting Startup
            UpdateInflightBounds();
        }
    }
    if (mode_ == Mode::kDrain) {
        uint64_t bdp = BdpBytes(1000, 1000);
        if (bytes_in_flight_ <= bdp) {
            mode_ = Mode::kProbeBw;
            pacing_gain_ = 1.0;
            cwnd_gain_ = 2.0;
            EnterProbeBwState(ProbeBwState::kDown, now_us);
            cycle_index_ = 0;
            cycle_start_us_ = now_us;
        }
    }
}

void BBRv3CongestionControl::UpdatePacingRate() {
    if (!pacer_) return;
    pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void BBRv3CongestionControl::StartNewRound(uint64_t pn) {
    // Don't update end_of_round_pn_ here - it's updated by OnPacketSent
    // This prevents immediate re-triggering of new round
    (void)pn;
    round_count_++;
    round_start_pn_ = pn;
    round_delivered_bytes_ = 0;
    round_lost_bytes_ = 0;
    ecn_seen_in_round_ = false;
    rounds_since_probe_++;
}

void BBRv3CongestionControl::AdaptInflightBoundsOnLoss(uint64_t now_us) {
    (void)now_us;
    // Only calculate loss rate if we have meaningful data
    if (round_delivered_bytes_ == 0 && round_lost_bytes_ == 0) return;
    
    // Calculate loss rate: lost / (delivered + lost)
    uint64_t total = round_delivered_bytes_ + round_lost_bytes_;
    if (total == 0) return;
    
    double loss_rate = static_cast<double>(round_lost_bytes_) / static_cast<double>(total);
    if (loss_rate > loss_thresh_) {
        // Reduce inflight_hi on excessive loss using beta_loss
        inflight_hi_bytes_ = std::max<uint64_t>(
            inflight_lo_bytes_, 
            static_cast<uint64_t>(inflight_hi_bytes_ * beta_loss_)
        );
    } else if (round_lost_bytes_ == 0) {
        // Only raise hi when there's no loss in this round
        inflight_hi_bytes_ = std::min<uint64_t>(inflight_hi_bytes_ + 2 * cfg_.mss_bytes, cfg_.max_cwnd_bytes);
    }
    // If there's some loss but below threshold, keep hi unchanged
}

void BBRv3CongestionControl::AdaptOnEcn() {
    if (!ecn_seen_in_round_) return;
    
    // ECN marks indicate congestion before loss occurs
    // React more conservatively than loss
    inflight_hi_bytes_ = std::max<uint64_t>(
        inflight_lo_bytes_,
        static_cast<uint64_t>(inflight_hi_bytes_ * beta_ecn_)
    );
    
    // Exit Startup if we see ECN marks early
    if (mode_ == Mode::kStartup) {
        mode_ = Mode::kDrain;
        pacing_gain_ = 1.0 / 2.885;
        cwnd_gain_ = 2.0;
    }
    
    // Exit Probe UP state on ECN
    if (mode_ == Mode::kProbeBw && probe_bw_state_ == ProbeBwState::kUp) {
        EnterProbeBwState(ProbeBwState::kDown, 0);
    }
}

void BBRv3CongestionControl::UpdateInflightBounds() {
    // Set inflight_lo based on current BDP estimate
    // This provides a reasonable lower bound for inflight data
    if (max_bw_bps_ > 0 && min_rtt_us_ > 0) {
        uint64_t bdp = BdpBytes(1000, 1000);
        
        if (mode_ == Mode::kStartup) {
            // No lower bound during startup
            inflight_lo_bytes_ = 0;
        } else {
            // Set to 50% of BDP as lower bound
            inflight_lo_bytes_ = bdp / 2;
            // Ensure it's at least min_cwnd
            inflight_lo_bytes_ = std::max<uint64_t>(inflight_lo_bytes_, cfg_.min_cwnd_bytes);
        }
    }
}

void BBRv3CongestionControl::EnterProbeBwState(ProbeBwState state, uint64_t now_us) {
    probe_bw_state_ = state;
    probe_bw_state_start_us_ = now_us;
    
    // Set pacing gain based on state
    switch (state) {
        case ProbeBwState::kDown:
            pacing_gain_ = 0.75;  // Drain queue
            break;
        case ProbeBwState::kCruise:
            pacing_gain_ = 1.0;   // Maintain current rate
            break;
        case ProbeBwState::kRefill:
            pacing_gain_ = 1.0;   // Fill pipe before probing
            break;
        case ProbeBwState::kUp:
            pacing_gain_ = 1.25;  // Probe for more bandwidth
            break;
    }
}

bool BBRv3CongestionControl::ShouldAdvanceProbeBwState(uint64_t now_us) const {
    if (probe_bw_state_start_us_ == 0) return false;
    
    uint64_t min_duration_us = std::max<uint64_t>(min_rtt_us_, 1000);
    uint64_t elapsed_us = now_us - probe_bw_state_start_us_;
    
    switch (probe_bw_state_) {
        case ProbeBwState::kDown:
            // Stay in DOWN until inflight drops below target OR timeout
            // Add timeout to prevent infinite blocking (max 5 RTTs)
            if (elapsed_us >= min_duration_us * 5) 
            return true;
            return elapsed_us >= min_duration_us && 
                   bytes_in_flight_ <= BdpBytes(1000, 1000);
        
        case ProbeBwState::kCruise:
            // Cruise for multiple RTTs before next probe
            return elapsed_us >= min_duration_us * 3;
        
        case ProbeBwState::kRefill:
            // Refill for one RTT
            return elapsed_us >= min_duration_us;
        
        case ProbeBwState::kUp:
            // Probe for one RTT
            return elapsed_us >= min_duration_us;
        
        default:
            return false;
    }
}

} // namespace quic
} // namespace quicx


