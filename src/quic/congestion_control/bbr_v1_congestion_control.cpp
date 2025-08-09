#include "quic/congestion_control/bbr_v1_congestion_control.h"
#include "quic/congestion_control/normal_pacer.h"
#include <algorithm>

namespace quicx {
namespace quic {

static inline uint64_t MulDiv(uint64_t a, uint64_t num, uint64_t den) {
    if (den == 0) return 0;
    __int128 t = static_cast<__int128>(a) * static_cast<__int128>(num);
    return static_cast<uint64_t>(t / den);
}

BBRv1CongestionControl::BBRv1CongestionControl() {
    Configure({});
}

BBRv1CongestionControl::~BBRv1CongestionControl() = default;

void BBRv1CongestionControl::Configure(const CcConfigV2& cfg) {
    cfg_ = cfg;
    cwnd_bytes_ = std::max<uint64_t>(cfg_.initial_cwnd_bytes, 4 * cfg_.mss_bytes);
    ssthresh_bytes_ = UINT64_MAX;
    bytes_in_flight_ = 0;

    srtt_us_ = 0;
    min_rtt_us_ = 0;
    min_rtt_stamp_us_ = 0;

    bw_window_.clear();
    max_bw_bps_ = 0;
    full_bw_bps_ = 0;
    full_bw_cnt_ = 0;
    end_of_round_pn_ = 0;

    mode_ = Mode::kStartup;
    pacing_gain_ = 2.885; // STARTUP gain ~ 2/ln(2)
    cwnd_gain_ = 2.0;

    cycle_index_ = 0;
    cycle_start_us_ = 0;

    probe_rtt_done_stamp_valid_ = false;
    probe_rtt_done_stamp_us_ = 0;

    pacer_.reset(new NormalPacer());
}

void BBRv1CongestionControl::OnPacketSent(const SentPacketEvent& ev) {
    bytes_in_flight_ += ev.bytes;
    if (pacer_) pacer_->OnPacketSent(ev.sent_time, static_cast<size_t>(ev.bytes));
}

void BBRv1CongestionControl::OnPacketAcked(const AckEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_acked) ? bytes_in_flight_ - ev.bytes_acked : 0;

    // bandwidth sample: acked_bytes / srtt
    if (srtt_us_ > 0) {
        uint64_t sample_bps = MulDiv(ev.bytes_acked, 8ull * 1000000ull, srtt_us_); // bits/sec
        sample_bps /= 8; // convert to bytes/sec
        UpdateMaxBandwidth(sample_bps, ev.ack_time);
    }

    CheckFullBandwidthReached(ev.ack_time);
    MaybeEnterOrExitProbeRtt(ev.ack_time);

    // Update cwnd based on BDP * cwnd_gain_
    uint64_t target = BdpBytes(static_cast<uint64_t>(cwnd_gain_ * 1000), 1000);
    if (cwnd_bytes_ < target) cwnd_bytes_ += ev.bytes_acked; // grow by acked bytes
    cwnd_bytes_ = std::min<uint64_t>(cwnd_bytes_, target);
    cwnd_bytes_ = std::min<uint64_t>(cwnd_bytes_, cfg_.max_cwnd_bytes);

    // Advance ProbeBW cycle if needed
    if (mode_ == Mode::kProbeBw) {
        AdvanceProbeBwCycle(ev.ack_time);
    }
    UpdatePacingRate();
}

void BBRv1CongestionControl::OnPacketLost(const LossEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_lost) ? bytes_in_flight_ - ev.bytes_lost : 0;
    // In STARTUP, loss exits to DRAIN
    if (mode_ == Mode::kStartup) {
        mode_ = Mode::kDrain;
        pacing_gain_ = 1.0 / 2.885; // drain faster by pacing below bw
        cwnd_gain_ = 2.0;
    }
    UpdatePacingRate();
}

void BBRv1CongestionControl::OnRoundTripSample(uint64_t latest_rtt, uint64_t ack_delay) {
    (void)ack_delay;
    if (srtt_us_ == 0) srtt_us_ = latest_rtt;
    srtt_us_ = (7 * srtt_us_ + latest_rtt) / 8;
    if (min_rtt_us_ == 0 || latest_rtt < min_rtt_us_) {
        min_rtt_us_ = latest_rtt;
        min_rtt_stamp_us_ = 0; // force ProbeRTT timer refresh at caller time base
    }
}

ICongestionControl::SendState BBRv1CongestionControl::CanSend(uint64_t now, uint64_t& can_send_bytes) const {
    (void)now;
    uint64_t target = BdpBytes(static_cast<uint64_t>(cwnd_gain_ * 1000), 1000);
    uint64_t left = (target > bytes_in_flight_) ? (target - bytes_in_flight_) : 0;
    can_send_bytes = left;
    if (left == 0) return SendState::kBlockedByCwnd;
    return SendState::kOk;
}

uint64_t BBRv1CongestionControl::GetPacingRateBps() const {
    if (max_bw_bps_ == 0) {
        if (srtt_us_ == 0) return cfg_.min_cwnd_bytes * 8;
        // fallback: cwnd/srtt
        uint64_t bw_bytes_per_sec = MulDiv(cwnd_bytes_, 1000000ull, srtt_us_);
        return static_cast<uint64_t>(bw_bytes_per_sec * pacing_gain_);
    }
    return static_cast<uint64_t>(max_bw_bps_ * pacing_gain_);
}

uint64_t BBRv1CongestionControl::NextSendTime(uint64_t now) const {
    if (!pacer_) return 0;
    (void)now;
    return pacer_->TimeUntilSend();
}

uint64_t BBRv1CongestionControl::BdpBytes(uint64_t gain_num, uint64_t gain_den) const {
    if (min_rtt_us_ == 0) {
        // Fallback to SRTT or MSS-based cwnd
        return std::max<uint64_t>(cwnd_bytes_, 4 * cfg_.mss_bytes);
    }
    uint64_t bw = (max_bw_bps_ > 0) ? max_bw_bps_ : (srtt_us_ > 0 ? MulDiv(cwnd_bytes_, 1000000ull, srtt_us_) : cfg_.initial_cwnd_bytes);
    uint64_t bdp = MulDiv(bw, min_rtt_us_, 1000000ull); // bytes
    __int128 t = static_cast<__int128>(bdp) * static_cast<__int128>(gain_num);
    return static_cast<uint64_t>(t / gain_den);
}

void BBRv1CongestionControl::SetPacingGain(double gain) { pacing_gain_ = gain; }
void BBRv1CongestionControl::SetCwndGain(double gain) { cwnd_gain_ = gain; }
void BBRv1CongestionControl::UpdatePacingRate() {
    if (!pacer_) return;
    pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

void BBRv1CongestionControl::MaybeEnterOrExitProbeRtt(uint64_t now_us) {
    // Every 10s, enter ProbeRTT for 200ms
    if (!probe_rtt_done_stamp_valid_ || now_us - probe_rtt_done_stamp_us_ > kProbeRttIntervalUs) {
        mode_ = Mode::kProbeRtt;
        SetPacingGain(1.0);
        SetCwndGain(1.0);
        probe_rtt_done_stamp_valid_ = true;
        probe_rtt_done_stamp_us_ = now_us + kProbeRttTimeUs;
    }
    if (mode_ == Mode::kProbeRtt && now_us >= probe_rtt_done_stamp_us_) {
        // Return to PROBE_BW after ProbeRTT completes
        mode_ = Mode::kProbeBw;
        SetPacingGain(1.0);
        SetCwndGain(2.0);
        // Start so that the first cycle advance goes to index 1 (gain 0.75)
        cycle_index_ = 0;
        cycle_start_us_ = now_us;
    }
}

void BBRv1CongestionControl::AdvanceProbeBwCycle(uint64_t now_us) {
    static const double kGainCycle[8] = {1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    uint64_t cycle_len_us = std::max<uint64_t>(min_rtt_us_, 1000); // at least 1ms
    if (now_us - cycle_start_us_ >= cycle_len_us) {
        cycle_index_ = (cycle_index_ + 1) % 8;
        cycle_start_us_ = now_us;
        SetPacingGain(kGainCycle[cycle_index_]);
    }
}

void BBRv1CongestionControl::UpdateMaxBandwidth(uint64_t sample_bps, uint64_t now_us) {
    // sliding window max filter
    bw_window_.push_back({now_us, sample_bps});
    while (bw_window_.size() > kBwWindow) bw_window_.erase(bw_window_.begin());
    uint64_t m = 0;
    for (const auto& s : bw_window_) m = std::max<uint64_t>(m, s.bytes_per_sec);
    max_bw_bps_ = m;
}

void BBRv1CongestionControl::CheckFullBandwidthReached(uint64_t now_us) {
    (void)now_us;
    if (max_bw_bps_ == 0) return;
    if (full_bw_bps_ == 0 || max_bw_bps_ > full_bw_bps_ * 125 / 100) {
        full_bw_bps_ = max_bw_bps_;
        full_bw_cnt_ = 0;
    } else {
        full_bw_cnt_++;
        if (mode_ == Mode::kStartup && full_bw_cnt_ >= 3) {
            // Exit STARTUP to DRAIN
            mode_ = Mode::kDrain;
            SetPacingGain(1.0 / 2.885);
            SetCwndGain(2.0);
        }
    }
    if (mode_ == Mode::kDrain) {
        // Drain until in-flight <= BDP
        uint64_t bdp = BdpBytes(1000, 1000);
        if (bytes_in_flight_ <= bdp) {
            mode_ = Mode::kProbeBw;
            SetPacingGain(1.0);
            SetCwndGain(2.0);
            cycle_index_ = 0;
            cycle_start_us_ = now_us;
        }
    }
}

} // namespace quic
} // namespace quicx


