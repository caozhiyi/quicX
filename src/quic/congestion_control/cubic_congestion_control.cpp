#include <cmath>
#include <algorithm>
#include "quic/congestion_control/normal_pacer.h"
#include "quic/congestion_control/cubic_congestion_control.h"

// Define static constants
namespace quicx {
namespace quic {

constexpr double CubicCongestionControl::kBetaCubic;
constexpr double CubicCongestionControl::kC;
constexpr size_t CubicCongestionControl::kMinWindow;

CubicCongestionControl::CubicCongestionControl() :
    epoch_start_(0),
    w_max_(0),
    w_last_max_(0), 
    k_(0),
    origin_point_(0),
    tcp_friendliness_(true) {
    
    congestion_window_ = kMinWindow;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
    pacer_ = std::unique_ptr<NormalPacer>(new NormalPacer());
}

CubicCongestionControl::~CubicCongestionControl() {
}

void CubicCongestionControl::OnPacketSent(size_t bytes, uint64_t sent_time) {
    bytes_in_flight_ = bytes_in_flight_ + bytes;
    pacer_->OnPacketSent(sent_time, bytes);
}

void CubicCongestionControl::OnPacketAcked(size_t bytes, uint64_t ack_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;

    if (in_slow_start_) {
        // During slow start, cwnd grows by the number of bytes acknowledged
        congestion_window_ += bytes;
        if (congestion_window_ >= w_last_max_) {
            in_slow_start_ = false;
        }
    } else {
        if (epoch_start_ == 0) {
            epoch_start_ = ack_time;
            w_max_ = congestion_window_;
            k_ = std::cbrt((w_max_ * (1 - kBetaCubic)) / kC);
            origin_point_ = w_max_;
        }
        
        uint64_t elapsed_time = ack_time - epoch_start_;
        size_t target = CubicWindowSize(elapsed_time);
        
        if (tcp_friendliness_) {
            size_t tcp_window = TcpFriendlyWindowSize();
            target = std::max(target, tcp_window);
        }
        
        congestion_window_ = target;
    }
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

void CubicCongestionControl::OnPacketLost(size_t bytes, uint64_t lost_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;

    // Save cwnd before reducing it
    w_last_max_ = congestion_window_;
    
    // Multiplicative decrease
    congestion_window_ = std::max(kMinWindow, 
                                static_cast<size_t>(congestion_window_ * kBetaCubic));
    
    // Reset cubic state
    epoch_start_ = 0;
    w_max_ = congestion_window_;
    k_ = 0;
    in_slow_start_ = false;
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

void CubicCongestionControl::OnRttUpdated(uint64_t rtt) {
    smoothed_rtt_ = rtt;
    pacer_->OnPacingRateUpdated(GetPacingRate());
}

size_t CubicCongestionControl::GetCongestionWindow() const {
    return congestion_window_;
}

size_t CubicCongestionControl::GetBytesInFlight() const {
    return bytes_in_flight_;
}

bool CubicCongestionControl::CanSend(uint64_t now, uint32_t& can_send_bytes) const {
    uint32_t max_send_bytes = congestion_window_ - bytes_in_flight_;
    can_send_bytes = std::min(max_send_bytes, can_send_bytes);
    return pacer_->CanSend(now);
}

uint64_t CubicCongestionControl::GetPacingRate() const {
    if (smoothed_rtt_ == 0) {
        return kMinWindow; // Avoid division by zero
    }
    // Simple pacing rate calculation
    return congestion_window_ * 1000000 / smoothed_rtt_; // bytes per second
}

void CubicCongestionControl::Reset() {
    congestion_window_ = kMinWindow;
    bytes_in_flight_ = 0;
    epoch_start_ = 0;
    w_max_ = 0;
    w_last_max_ = 0;
    k_ = 0;
    in_slow_start_ = true;
}

size_t CubicCongestionControl::CubicWindowSize(uint64_t elapsed_time) {
    double t = elapsed_time / 1000000.0; // Convert to seconds
    double tx = t - k_;
    double w_cubic = kC * tx * tx * tx + origin_point_;
    return static_cast<size_t>(w_cubic);
}

size_t CubicCongestionControl::TcpFriendlyWindowSize() {
    // TCP Reno-like window growth
    double rtt = smoothed_rtt_ / 1000000.0; // Convert to seconds
    double w_tcp = w_max_ * kBetaCubic + 
                  (3 * (1 - kBetaCubic) / (1 + kBetaCubic)) * 
                  (congestion_window_ / rtt);
    return static_cast<size_t>(w_tcp);
}

}
}
