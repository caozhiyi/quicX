#include <algorithm>
#include <cmath>

#include "common/qlog/qlog.h"
#include "quic/congestion_control/cubic_congestion_control.h"
#include "quic/congestion_control/normal_pacer.h"

namespace quicx {
namespace quic {

static inline double BytesToPkts(uint64_t bytes, uint64_t mss) {
    return static_cast<double>(bytes) / static_cast<double>(mss);
}

static inline uint64_t PktsToBytes(double pkts, uint64_t mss) {
    double v = pkts * static_cast<double>(mss);
    if (v < 0.0) v = 0.0;
    return static_cast<uint64_t>(v);
}

CubicCongestionControl::CubicCongestionControl() {
    Configure({});
}

void CubicCongestionControl::Configure(const CcConfigV2& cfg) {
    cfg_ = cfg;
    cwnd_bytes_ = cfg_.initial_cwnd_bytes;
    ssthresh_bytes_ = UINT64_MAX;
    bytes_in_flight_ = 0;
    srtt_us_ = 0;

    w_max_pkts_ = BytesToPkts(cwnd_bytes_, cfg_.mss_bytes);
    w_last_pkts_ = w_max_pkts_;
    epoch_start_us_ = 0;
    k_time_sec_ = 0.0;

    in_slow_start_ = true;
    in_recovery_ = false;
    recovery_start_time_us_ = 0;

    // Initialize HyStart
    ResetHyStart();

    if (!pacer_) pacer_.reset(new NormalPacer());
}

void CubicCongestionControl::OnPacketSent(const SentPacketEvent& ev) {
    bytes_in_flight_ += ev.bytes;
    if (pacer_) pacer_->OnPacketSent(ev.sent_time, static_cast<size_t>(ev.bytes));
}

void CubicCongestionControl::OnPacketAcked(const AckEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_acked) ? bytes_in_flight_ - ev.bytes_acked : 0;

    // ECN-CE: treat as early congestion signal, exit slow start and reduce cwnd
    if (ev.ecn_ce) {
        if (in_slow_start_) in_slow_start_ = false;
        if (!in_recovery_) {
            // Fast Convergence: check if cwnd decreased since last congestion event
            double curr_w_max_pkts = BytesToPkts(cwnd_bytes_, cfg_.mss_bytes);
            if (curr_w_max_pkts < w_max_pkts_) {
                // Fast convergence: reduce W_max more aggressively
                w_max_pkts_ = curr_w_max_pkts * (2.0 - kBetaCubic) / 2.0;
            } else {
                w_max_pkts_ = curr_w_max_pkts;
            }

            // Reduce cwnd multiplicatively (similar to loss path)
            uint64_t new_cwnd = static_cast<uint64_t>(cwnd_bytes_ * kBetaCubic);
            cwnd_bytes_ = std::max<uint64_t>(new_cwnd, cfg_.min_cwnd_bytes);
            ssthresh_bytes_ = cwnd_bytes_;
            in_recovery_ = true;
            recovery_start_time_us_ = ev.ack_time;
            epoch_start_us_ = 0;  // force cubic epoch reset

            // Reset HyStart on congestion signal
            ResetHyStart();
        }
        if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
        return;
    }

    // Slow start phase with HyStart
    if (in_slow_start_) {
        cwnd_bytes_ += ev.bytes_acked;

        // Check traditional ssthresh exit
        if (cwnd_bytes_ >= ssthresh_bytes_) {
            in_slow_start_ = false;
            ResetEpoch(ev.ack_time);
        }
        // Check HyStart early exit (using srtt_us_ as latest RTT proxy)
        else if (hystart_enabled_ && srtt_us_ > 0 && CheckHyStartExit(srtt_us_, ev.ack_time)) {
            // Exit slow start early via HyStart
            ssthresh_bytes_ = cwnd_bytes_;
            in_slow_start_ = false;
            hystart_found_exit_ = true;
            ResetEpoch(ev.ack_time);
        }

        if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
        return;
    }

    if (in_recovery_) {
        if (ev.ack_time > recovery_start_time_us_) {
            in_recovery_ = false;
            ResetEpoch(ev.ack_time);
        } else {
            return;
        }
    }

    IncreaseOnAck(ev.bytes_acked, ev.ack_time);
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void CubicCongestionControl::OnPacketLost(const LossEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_lost) ? bytes_in_flight_ - ev.bytes_lost : 0;

    // Fast Convergence: if cwnd decreased since last loss, further reduce W_max
    double curr_w_max_pkts = BytesToPkts(cwnd_bytes_, cfg_.mss_bytes);
    if (curr_w_max_pkts < w_max_pkts_) {
        // Fast convergence: reduce W_max more aggressively
        w_max_pkts_ = curr_w_max_pkts * (2.0 - kBetaCubic) / 2.0;
    } else {
        w_max_pkts_ = curr_w_max_pkts;
    }

    // Multiplicative decrease
    uint64_t new_cwnd = static_cast<uint64_t>(cwnd_bytes_ * kBetaCubic);
    cwnd_bytes_ = std::max<uint64_t>(new_cwnd, cfg_.min_cwnd_bytes);
    ssthresh_bytes_ = cwnd_bytes_;

    in_recovery_ = true;
    in_slow_start_ = false;
    recovery_start_time_us_ = ev.lost_time;
    epoch_start_us_ = 0;  // force reset on next ACK

    // Reset HyStart on loss
    ResetHyStart();

    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void CubicCongestionControl::OnRoundTripSample(uint64_t latest_rtt, uint64_t ack_delay) {
    (void)ack_delay;
    if (srtt_us_ == 0) srtt_us_ = latest_rtt;
    srtt_us_ = (7 * srtt_us_ + latest_rtt) / 8;
    // Note: HyStart logic is handled in OnPacketAcked where ack_time is available
}

ICongestionControl::SendState CubicCongestionControl::CanSend(uint64_t now, uint64_t& can_send_bytes) const {
    (void)now;
    uint64_t left = (cwnd_bytes_ > bytes_in_flight_) ? (cwnd_bytes_ - bytes_in_flight_) : 0;
    can_send_bytes = left;
    if (left == 0) {
        return SendState::kBlockedByCwnd;
    }
    return SendState::kOk;
}

uint64_t CubicCongestionControl::GetPacingRateBps() const {
    // Pacing optimization: use proper initial RTT and apply pacing gain
    uint64_t rtt_us = srtt_us_;
    if (rtt_us == 0) {
        // Use QUIC default initial RTT: 333ms
        rtt_us = 333000;
    }

    // CUBIC pacing: use 1.25x gain for better burst smoothing
    // rate = (cwnd * 1.25) / RTT
    return (cwnd_bytes_ * 8ull * 1000000ull * 5) / (rtt_us * 4);
}

uint64_t CubicCongestionControl::NextSendTime(uint64_t now) const {
    (void)now;
    if (!pacer_) return 0;
    return pacer_->TimeUntilSend();
}

void CubicCongestionControl::ResetEpoch(uint64_t now) {
    epoch_start_us_ = now;
    double w_c_pkts = BytesToPkts(cwnd_bytes_, cfg_.mss_bytes);
    // K = cbrt((Wmax - Wc)/C)
    double diff = w_max_pkts_ - w_c_pkts;
    if (diff < 0) diff = 0;
    // Use pow for cubic root for maximal portability
    k_time_sec_ = std::pow(diff / kCubicC, 1.0 / 3.0);
    w_last_pkts_ = w_c_pkts;
}

void CubicCongestionControl::IncreaseOnAck(uint64_t bytes_acked, uint64_t now) {
    if (epoch_start_us_ == 0) ResetEpoch(now);

    // t = (now - epoch_start)/1e6
    double t_sec = static_cast<double>(now - epoch_start_us_) / 1e6;
    double t_k = t_sec - k_time_sec_;
    if (t_k < 0) t_k = -t_k;  // account for pre-K period

    // CUBIC window in packets: W_cubic(t) = C*(t - K)^3 + Wmax
    double w_cubic_pkts = kCubicC * t_k * t_k * t_k + w_max_pkts_;

    // TCP-friendly Reno variant for fairness: W_reno = W_last + (3*bytes_acked)/(2*W_last) in packets
    double w_last = w_last_pkts_;
    if (w_last < 1.0) w_last = 1.0;
    double w_reno_pkts = w_last + (3.0 * BytesToPkts(bytes_acked, cfg_.mss_bytes)) / (2.0 * w_last);

    // Choose the larger of cubic and reno
    double w_target_pkts = std::max(w_cubic_pkts, w_reno_pkts);
    uint64_t target_bytes = PktsToBytes(w_target_pkts, cfg_.mss_bytes);

    if (target_bytes > cwnd_bytes_) {
        cwnd_bytes_ = target_bytes;
        cwnd_bytes_ = std::min<uint64_t>(cwnd_bytes_, cfg_.max_cwnd_bytes);
    }

    w_last_pkts_ = BytesToPkts(cwnd_bytes_, cfg_.mss_bytes);
}

void CubicCongestionControl::ResetHyStart() {
    hystart_min_rtt_us_ = UINT64_MAX;
    hystart_rtt_sample_count_ = 0;
    hystart_current_round_min_rtt_us_ = UINT64_MAX;
    hystart_last_round_min_rtt_us_ = UINT64_MAX;
    hystart_round_start_us_ = 0;
    hystart_last_ack_time_us_ = 0;
    hystart_found_exit_ = false;
}

bool CubicCongestionControl::CheckHyStartExit(uint64_t latest_rtt, uint64_t now) {
    if (hystart_found_exit_) {
        return false;
    }

    // Only apply HyStart when cwnd is above a low threshold
    double cwnd_pkts = BytesToPkts(cwnd_bytes_, cfg_.mss_bytes);
    if (cwnd_pkts < kHyStartLowWindow) {
        return false;
    }

    // Initialize round on first call
    if (hystart_round_start_us_ == 0) {
        hystart_round_start_us_ = now;
        hystart_current_round_min_rtt_us_ = latest_rtt;
        return false;
    }

    // Update current round min RTT
    if (latest_rtt < hystart_current_round_min_rtt_us_) {
        hystart_current_round_min_rtt_us_ = latest_rtt;
    }

    // Update global min RTT
    if (latest_rtt < hystart_min_rtt_us_) {
        hystart_min_rtt_us_ = latest_rtt;
    }

    ++hystart_rtt_sample_count_;

    // Check 1: RTT increase detection
    // If current round's min RTT increased significantly from baseline, exit
    if (hystart_rtt_sample_count_ >= kHyStartMinSamples) {
        if (hystart_current_round_min_rtt_us_ > hystart_min_rtt_us_ + kHyStartRttThreshUs) {
            return true;  // Exit slow start due to RTT increase
        }
    }

    // Check 2: ACK train detection
    // If ACKs arrive too far apart, network may be congested
    if (hystart_last_ack_time_us_ > 0) {
        uint64_t ack_delta = (now > hystart_last_ack_time_us_) ? (now - hystart_last_ack_time_us_) : 0;
        if (ack_delta > kHyStartAckDeltaUs) {
            return true;  // Exit slow start due to ACK spacing
        }
    }
    hystart_last_ack_time_us_ = now;

    // Start new round if enough RTT samples collected
    if (hystart_rtt_sample_count_ >= kHyStartMinSamples * 2) {
        hystart_last_round_min_rtt_us_ = hystart_current_round_min_rtt_us_;
        hystart_current_round_min_rtt_us_ = UINT64_MAX;
        hystart_round_start_us_ = now;
        hystart_rtt_sample_count_ = 0;
    }

    return false;
}

void CubicCongestionControl::SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) {
    qlog_trace_ = trace;
}

}  // namespace quic
}  // namespace quicx
