#include <algorithm>
#include <cmath>
#include "quic/congestion_control/normal_pacer.h"
#include "quic/congestion_control/cubic_congestion_control.h"

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
    if (!pacer_) pacer_.reset(new NormalPacer());
}

void CubicCongestionControl::OnPacketSent(const SentPacketEvent& ev) {
    bytes_in_flight_ += ev.bytes;
    if (pacer_) pacer_->OnPacketSent(ev.sent_time, static_cast<size_t>(ev.bytes));
}

void CubicCongestionControl::OnPacketAcked(const AckEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_acked) ? bytes_in_flight_ - ev.bytes_acked : 0;

    // Slow start phase
    if (in_slow_start_) {
        cwnd_bytes_ += ev.bytes_acked;
        if (cwnd_bytes_ >= ssthresh_bytes_) {
            in_slow_start_ = false;
            ResetEpoch(ev.ack_time);
        }
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

    // Update Wmax to last cwnd before loss
    w_max_pkts_ = BytesToPkts(cwnd_bytes_, cfg_.mss_bytes);
    // Multiplicative decrease
    uint64_t new_cwnd = static_cast<uint64_t>(cwnd_bytes_ * kBetaCubic);
    cwnd_bytes_ = std::max<uint64_t>(new_cwnd, cfg_.min_cwnd_bytes);
    ssthresh_bytes_ = cwnd_bytes_;

    in_recovery_ = true;
    in_slow_start_ = false;
    recovery_start_time_us_ = ev.lost_time;
    epoch_start_us_ = 0; // force reset on next ACK
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void CubicCongestionControl::OnRoundTripSample(uint64_t latest_rtt, uint64_t ack_delay) {
    (void)ack_delay;
    if (srtt_us_ == 0) srtt_us_ = latest_rtt;
    srtt_us_ = (7 * srtt_us_ + latest_rtt) / 8;
}

ICongestionControl::SendState CubicCongestionControl::CanSend(uint64_t now, uint64_t& can_send_bytes) const {
    (void)now;
    uint64_t left = (cwnd_bytes_ > bytes_in_flight_) ? (cwnd_bytes_ - bytes_in_flight_) : 0;
    can_send_bytes = left;
    if (left == 0) return SendState::kBlockedByCwnd;
    return SendState::kOk;
}

uint64_t CubicCongestionControl::GetPacingRateBps() const {
    if (srtt_us_ == 0) return cfg_.min_cwnd_bytes * 8;
    return (cwnd_bytes_ * 8ull * 1000000ull) / srtt_us_;
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
    if (t_k < 0) t_k = -t_k; // account for pre-K period

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

} // namespace quic
} // namespace quicx


