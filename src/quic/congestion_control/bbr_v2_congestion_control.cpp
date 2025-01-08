#include <algorithm>
#include "quic/congestion_control/normal_pacer.h"
#include "quic/congestion_control/bbr_v2_congestion_control.h"

namespace quicx {
namespace quic {

// BBRv2 specific parameters
constexpr double BBRv2CongestionControl::HIGH_GAIN;
constexpr double BBRv2CongestionControl::DRAIN_GAIN;
constexpr double BBRv2CongestionControl::PACING_GAIN;
constexpr double BBRv2CongestionControl::LOW_GAIN;
constexpr size_t BBRv2CongestionControl::MIN_WINDOW;
constexpr uint64_t BBRv2CongestionControl::PROBE_RTT_INTERVAL;
constexpr uint64_t BBRv2CongestionControl::PROBE_RTT_DURATION; 
constexpr double BBRv2CongestionControl::BETA_ECN;
constexpr double BBRv2CongestionControl::BETA_LOSS;


BBRv2CongestionControl::BBRv2CongestionControl() :
    mode_(STARTUP),
    min_rtt_timestamp_(0),
    probe_rtt_done_timestamp_(0),
    probe_rtt_round_done_(false),
    pacing_gain_(HIGH_GAIN),
    cwnd_gain_(HIGH_GAIN),
    max_bandwidth_(0),
    loss_rounds_(0),
    ecn_ce_rounds_(0),
    inflight_too_high_(false) {
    
    congestion_window_ = MIN_WINDOW;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
    pacer_ = std::make_unique<NormalPacer>();
}

BBRv2CongestionControl::~BBRv2CongestionControl() {
}

void BBRv2CongestionControl::OnPacketSent(size_t bytes, uint64_t sent_time) {
    bytes_in_flight_ = bytes_in_flight_ + bytes;
    pacer_->OnPacketSent(sent_time, bytes);
}

void BBRv2CongestionControl::OnPacketAcked(size_t bytes, uint64_t ack_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;

    // Calculate delivery rate
    uint64_t bandwidth = bytes * 1000000 / smoothed_rtt_; // bytes per second
    UpdateBandwidth(bandwidth, ack_time, false);

    CheckCyclePhase(ack_time);
    UpdateInflightModel();
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

void BBRv2CongestionControl::OnPacketLost(size_t bytes, uint64_t lost_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;
    loss_rounds_++;
    
    // Reduce congestion window on loss
    if (loss_rounds_ > 1) {
        congestion_window_ = std::max(MIN_WINDOW, 
            static_cast<size_t>(congestion_window_ * BETA_LOSS));
        inflight_too_high_ = true;
    }
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

void BBRv2CongestionControl::OnRttUpdated(uint64_t rtt) {
    smoothed_rtt_ = rtt;
    if (min_rtt_timestamp_ == 0 || rtt < min_rtt_) {
        min_rtt_ = rtt;
        min_rtt_timestamp_ = smoothed_rtt_;
    }
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

size_t BBRv2CongestionControl::GetCongestionWindow() const {
    if (mode_ == PROBE_RTT) {
        return MIN_WINDOW;
    }
    return std::min(GetTargetCwnd(), max_congestion_window_);
}

size_t BBRv2CongestionControl::GetBytesInFlight() const {
    return bytes_in_flight_;
}

bool BBRv2CongestionControl::CanSend(uint64_t now, uint32_t& can_send_bytes) const {
    uint32_t max_send_bytes = GetCongestionWindow() - bytes_in_flight_;
    can_send_bytes = std::min(max_send_bytes, can_send_bytes);
    return pacer_->CanSend(now);
}

uint64_t BBRv2CongestionControl::GetPacingRate() const {
    if (mode_ == PROBE_RTT) {
        // in PROBE_RTT, use conservative rate
        return max_bandwidth_;
    }
    
    // calculate bandwidth
    uint64_t bandwidth = max_bandwidth_;
    if (bandwidth == 0) {
        // if no bandwidth samples, use conservative estimate based on window
        if (min_rtt_ > 0) {
            bandwidth = (congestion_window_ * 1000000) / min_rtt_;
        } else {
            // if no RTT, use conservative default value
            bandwidth = (congestion_window_ * 1000000) / 100000;  // TODO: assume 100ms RTT
        }
    }

    // apply pacing gain
    uint64_t pacing_rate = bandwidth * pacing_gain_;

    // ensure minimum pacing rate
    const uint64_t min_pacing_rate = (MIN_WINDOW * 1000000) / 
        std::max(min_rtt_, static_cast<uint64_t>(1000)); // at least 1ms
    pacing_rate = std::max(pacing_rate, min_pacing_rate);

    return pacing_rate;
}

void BBRv2CongestionControl::Reset() {
    mode_ = STARTUP;
    min_rtt_timestamp_ = 0;
    probe_rtt_done_timestamp_ = 0;
    probe_rtt_round_done_ = false;
    pacing_gain_ = HIGH_GAIN;
    cwnd_gain_ = HIGH_GAIN;
    max_bandwidth_ = 0;
    loss_rounds_ = 0;
    ecn_ce_rounds_ = 0;
    inflight_too_high_ = false;
    bandwidth_samples_.clear();
    congestion_window_ = MIN_WINDOW;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
}

void BBRv2CongestionControl::UpdateBandwidth(uint64_t bandwidth, uint64_t timestamp, bool ecn_ce) {
    bandwidth_samples_.push_back({bandwidth, timestamp, ecn_ce});
    while (!bandwidth_samples_.empty() && 
           timestamp - bandwidth_samples_.front().timestamp > smoothed_rtt_) {
        bandwidth_samples_.pop_front();
    }

    uint64_t max_bw = 0;
    for (const auto& sample : bandwidth_samples_) {
        max_bw = std::max(max_bw, sample.bandwidth);
        if (sample.ecn_ce) {
            HandleEcnFeedback(true);
        }
    }
    max_bandwidth_ = max_bw;
}

void BBRv2CongestionControl::EnterStartup() {
    mode_ = STARTUP;
    pacing_gain_ = HIGH_GAIN;
    cwnd_gain_ = HIGH_GAIN;
}

void BBRv2CongestionControl::EnterDrain() {
    mode_ = DRAIN;
    pacing_gain_ = DRAIN_GAIN;
    cwnd_gain_ = HIGH_GAIN;
}

void BBRv2CongestionControl::EnterProbeBW() {
    mode_ = PROBE_BW;
    pacing_gain_ = PACING_GAIN;
    cwnd_gain_ = 2;
}

void BBRv2CongestionControl::EnterProbeRTT() {
    mode_ = PROBE_RTT;
    pacing_gain_ = 1;
    cwnd_gain_ = 1;
}

void BBRv2CongestionControl::CheckCyclePhase(uint64_t now) {
    if (mode_ == STARTUP && !bandwidth_samples_.empty() && 
        bandwidth_samples_.back().bandwidth <= max_bandwidth_) {
        EnterDrain();
    }

    if (mode_ == DRAIN && bytes_in_flight_ <= GetTargetCwnd()) {
        EnterProbeBW();
    }

    if (now - min_rtt_timestamp_ > PROBE_RTT_INTERVAL) {
        EnterProbeRTT();
        probe_rtt_done_timestamp_ = now + PROBE_RTT_DURATION;
        probe_rtt_round_done_ = false;
    }

    if (mode_ == PROBE_RTT && now > probe_rtt_done_timestamp_ && probe_rtt_round_done_) {
        min_rtt_timestamp_ = now;
        EnterProbeBW();
    }
}

uint64_t BBRv2CongestionControl::GetTargetCwnd() const {
    uint64_t bdp = max_bandwidth_ * std::min(smoothed_rtt_, min_rtt_) / 1000000;
    return bdp * cwnd_gain_;
}

void BBRv2CongestionControl::AdaptLowerBounds() {
    if (loss_rounds_ > 0 || ecn_ce_rounds_ > 0) {
        // Adapt minimum window based on loss/ECN signals
        size_t min_cwnd = std::max(MIN_WINDOW, 
            static_cast<size_t>(GetTargetCwnd() * std::min(BETA_LOSS, BETA_ECN)));
        congestion_window_ = std::max(congestion_window_, min_cwnd);
    }
}

void BBRv2CongestionControl::HandleEcnFeedback(bool ecn_ce) {
    if (ecn_ce) {
        ecn_ce_rounds_++;
        if (ecn_ce_rounds_ > 1) {
            congestion_window_ = std::max(MIN_WINDOW, 
                static_cast<size_t>(congestion_window_ * BETA_ECN));
            inflight_too_high_ = true;
        }
    }
}

void BBRv2CongestionControl::UpdateInflightModel() {
    if (inflight_too_high_) {
        // Gradually recover from inflight reduction
        if (loss_rounds_ == 0 && ecn_ce_rounds_ == 0) {
            inflight_too_high_ = false;
        }
    }
    AdaptLowerBounds();
}

}
}
