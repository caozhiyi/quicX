#include <algorithm>
#include "quic/congestion_control/bbr_v1_congestion_control.h"

namespace quicx {
namespace quic {

constexpr double BBRv1CongestionControl::HIGH_GAIN;
constexpr double BBRv1CongestionControl::DRAIN_GAIN;
constexpr double BBRv1CongestionControl::PACING_GAIN;
constexpr double BBRv1CongestionControl::LOW_GAIN;
constexpr size_t BBRv1CongestionControl::MIN_WINDOW;
constexpr uint64_t BBRv1CongestionControl::PROBE_RTT_INTERVAL;
constexpr uint64_t BBRv1CongestionControl::PROBE_RTT_DURATION;

BBRv1CongestionControl::BBRv1CongestionControl() :
    mode_(STARTUP),
    min_rtt_timestamp_(0),
    probe_rtt_done_timestamp_(0),
    probe_rtt_round_done_(false),
    pacing_gain_(HIGH_GAIN),
    cwnd_gain_(HIGH_GAIN),
    max_bandwidth_(0) {
    
    congestion_window_ = MIN_WINDOW;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
}

BBRv1CongestionControl::~BBRv1CongestionControl() {
}

void BBRv1CongestionControl::OnPacketSent(size_t bytes, uint64_t sent_time) {
    bytes_in_flight_ = bytes_in_flight_ + bytes;
}

void BBRv1CongestionControl::OnPacketAcked(size_t bytes, uint64_t ack_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;

    // Calculate delivery rate
    uint64_t bandwidth = bytes * 1000000 / smoothed_rtt_; // bytes per second
    UpdateBandwidth(bandwidth, ack_time);

    CheckCyclePhase(ack_time);
}

void BBRv1CongestionControl::OnPacketLost(size_t bytes, uint64_t lost_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;
}

void BBRv1CongestionControl::OnRttUpdated(uint64_t rtt) {
    smoothed_rtt_ = rtt;
    if (min_rtt_timestamp_ == 0 || rtt < min_rtt_) {
        min_rtt_ = rtt;
        min_rtt_timestamp_ = smoothed_rtt_;
    }
}

size_t BBRv1CongestionControl::GetCongestionWindow() const {
    if (mode_ == PROBE_RTT) {
        return MIN_WINDOW;
    }
    return std::min(GetTargetCwnd(), max_congestion_window_);
}

size_t BBRv1CongestionControl::GetBytesInFlight() const {
    return bytes_in_flight_;
}

bool BBRv1CongestionControl::CanSend(size_t bytes_in_flight) const {
    return bytes_in_flight < GetCongestionWindow();
}

uint64_t BBRv1CongestionControl::GetPacingRate() const {
    return max_bandwidth_ * pacing_gain_;
}

void BBRv1CongestionControl::Reset() {
    mode_ = STARTUP;
    min_rtt_timestamp_ = 0;
    probe_rtt_done_timestamp_ = 0;
    probe_rtt_round_done_ = false;
    pacing_gain_ = HIGH_GAIN;
    cwnd_gain_ = HIGH_GAIN;
    max_bandwidth_ = 0;
    bandwidth_samples_.clear();
    congestion_window_ = MIN_WINDOW;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
}

void BBRv1CongestionControl::UpdateBandwidth(uint64_t bandwidth, uint64_t timestamp) {
    bandwidth_samples_.push_back({bandwidth, timestamp});
    while (!bandwidth_samples_.empty() && 
           timestamp - bandwidth_samples_.front().timestamp > smoothed_rtt_) {
        bandwidth_samples_.pop_front();
    }

    uint64_t max_bw = 0;
    for (const auto& sample : bandwidth_samples_) {
        max_bw = std::max(max_bw, sample.bandwidth);
    }
    max_bandwidth_ = max_bw;
}

void BBRv1CongestionControl::EnterStartup() {
    mode_ = STARTUP;
    pacing_gain_ = HIGH_GAIN;
    cwnd_gain_ = HIGH_GAIN;
}

void BBRv1CongestionControl::EnterDrain() {
    mode_ = DRAIN;
    pacing_gain_ = DRAIN_GAIN;
    cwnd_gain_ = HIGH_GAIN;
}

void BBRv1CongestionControl::EnterProbeBW() {
    mode_ = PROBE_BW;
    pacing_gain_ = PACING_GAIN;
    cwnd_gain_ = 2;
}

void BBRv1CongestionControl::EnterProbeRTT() {
    mode_ = PROBE_RTT;
    pacing_gain_ = 1;
    cwnd_gain_ = 1;
}

void BBRv1CongestionControl::CheckCyclePhase(uint64_t now) {
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

uint64_t BBRv1CongestionControl::GetTargetCwnd() const {
    return max_bandwidth_ * std::min(smoothed_rtt_, min_rtt_) * cwnd_gain_ / 1000000;
}

}
}
