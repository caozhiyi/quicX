#include <algorithm>
#include "quic/congestion_control/reno_congestion_control.h"

namespace quicx {
namespace quic {

constexpr size_t RenoCongestionControl::INITIAL_WINDOW;
constexpr double RenoCongestionControl::BETA_RENO;
constexpr size_t RenoCongestionControl::MIN_WINDOW;

RenoCongestionControl::RenoCongestionControl() :
    ssthresh_(UINT64_MAX),
    recovery_start_(0),
    in_recovery_(false) {
    
    congestion_window_ = INITIAL_WINDOW;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
}

RenoCongestionControl::~RenoCongestionControl() {
}

void RenoCongestionControl::OnPacketSent(size_t bytes, uint64_t sent_time) {
    bytes_in_flight_ = bytes_in_flight_ + bytes;
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
        congestion_window_ += (INITIAL_WINDOW * bytes) / congestion_window_;
    }
}

void RenoCongestionControl::OnPacketLost(size_t bytes, uint64_t lost_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;
    if (!in_recovery_) {
        ssthresh_ = congestion_window_ * BETA_RENO;
        congestion_window_ = std::max(MIN_WINDOW, ssthresh_);
        in_recovery_ = true;
        recovery_start_ = lost_time;
        in_slow_start_ = false;
    }
}

void RenoCongestionControl::OnRttUpdated(uint64_t rtt) {
    smoothed_rtt_ = rtt;
}

size_t RenoCongestionControl::GetCongestionWindow() const {
    return congestion_window_;
}

size_t RenoCongestionControl::GetBytesInFlight() const {
    return bytes_in_flight_;
}

bool RenoCongestionControl::CanSend(size_t bytes_in_flight) const {
    return bytes_in_flight < congestion_window_;
}

uint64_t RenoCongestionControl::GetPacingRate() const {
    // Simple pacing rate calculation
    return congestion_window_ * 1000000 / smoothed_rtt_; // bytes per second
}

void RenoCongestionControl::Reset() {
    congestion_window_ = INITIAL_WINDOW;
    bytes_in_flight_ = 0;
    ssthresh_ = UINT64_MAX;
    in_recovery_ = false;
    recovery_start_ = 0;
    in_slow_start_ = true;
}

}
}
