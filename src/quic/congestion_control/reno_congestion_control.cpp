#include <algorithm>
#include "quic/congestion_control/normal_pacer.h"
#include "quic/congestion_control/reno_congestion_control.h"

namespace quicx {
namespace quic {

constexpr size_t RenoCongestionControl::kInitialWindow;
constexpr double RenoCongestionControl::kBetaReno;
constexpr size_t RenoCongestionControl::kMinWindow;

RenoCongestionControl::RenoCongestionControl() :
    ssthresh_(UINT64_MAX),
    recovery_start_(0),
    in_recovery_(false) {
    
    congestion_window_ = kInitialWindow;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
    pacer_ = std::unique_ptr<NormalPacer>(new NormalPacer());
}

RenoCongestionControl::~RenoCongestionControl() {

}

void RenoCongestionControl::OnPacketSent(size_t bytes, uint64_t sent_time) {
    bytes_in_flight_ = bytes_in_flight_ + bytes;
    pacer_->OnPacketSent(sent_time, bytes);
}

void RenoCongestionControl::OnPacketAcked(size_t bytes, uint64_t ack_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;

    if (in_recovery_) {
        // In recovery phase, don't increase window
        if (ack_time > recovery_start_) {
            in_recovery_ = false;
        }
        return;
    }

    if (in_slow_start_) {
        // In slow start, grow window exponentially
        congestion_window_ += bytes;
        if (congestion_window_ >= ssthresh_) {
            in_slow_start_ = false;
        }
    } else {
        // In congestion avoidance, grow window linearly
        congestion_window_ += (kInitialWindow * bytes) / congestion_window_;
    }
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

void RenoCongestionControl::OnPacketLost(size_t bytes, uint64_t lost_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;
    if (!in_recovery_) {
        ssthresh_ = congestion_window_ * kBetaReno;
        congestion_window_ = std::max(kMinWindow, ssthresh_);
        in_recovery_ = true;
        recovery_start_ = lost_time;
        in_slow_start_ = false;
    }
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

void RenoCongestionControl::OnRttUpdated(uint64_t rtt) {
    smoothed_rtt_ = rtt;
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

size_t RenoCongestionControl::GetCongestionWindow() const {
    return congestion_window_;
}

size_t RenoCongestionControl::GetBytesInFlight() const {
    return bytes_in_flight_;
}

bool RenoCongestionControl::CanSend(uint64_t now, uint32_t& can_send_bytes) const {
    uint32_t max_send_bytes = congestion_window_ - bytes_in_flight_;
    can_send_bytes = std::min(max_send_bytes, can_send_bytes);
    return pacer_->CanSend(now);
}

uint64_t RenoCongestionControl::GetPacingRate() const {
     if (smoothed_rtt_ == 0) {
        return kMinWindow; // Avoid division by zero
    }
    // Simple pacing rate calculation
    return congestion_window_ * 1000000 / smoothed_rtt_; // bytes per second
}

void RenoCongestionControl::Reset() {
    congestion_window_ = kInitialWindow;
    bytes_in_flight_ = 0;
    ssthresh_ = UINT64_MAX;
    in_recovery_ = false;
    recovery_start_ = 0;
    in_slow_start_ = true;
}

}
}
