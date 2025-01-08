#include <cmath>
#include <algorithm>
#include "quic/congestion_control/cubic_congestion_control.h"

// Define static constants
namespace quicx {
namespace quic {

constexpr double CubicCongestionControl::BETA_CUBIC;
constexpr double CubicCongestionControl::C;
constexpr size_t CubicCongestionControl::MIN_WINDOW;

CubicCongestionControl::CubicCongestionControl() :
    epoch_start_(0),
    w_max_(0),
    w_last_max_(0), 
    k_(0),
    origin_point_(0),
    tcp_friendliness_(true) {
    
    congestion_window_ = MIN_WINDOW;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
}

CubicCongestionControl::~CubicCongestionControl() {
}

void CubicCongestionControl::OnPacketSent(size_t bytes, uint64_t sent_time) {
    bytes_in_flight_ = bytes_in_flight_ + bytes;
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
            k_ = std::cbrt((w_max_ * (1 - BETA_CUBIC)) / C);
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
}

void CubicCongestionControl::OnPacketLost(size_t bytes, uint64_t lost_time) {
    bytes_in_flight_ = (bytes_in_flight_ > bytes) ? bytes_in_flight_ - bytes : 0;

    // Save cwnd before reducing it
    w_last_max_ = congestion_window_;
    
    // Multiplicative decrease
    congestion_window_ = std::max(MIN_WINDOW, 
                                static_cast<size_t>(congestion_window_ * BETA_CUBIC));
    
    // Reset cubic state
    epoch_start_ = 0;
    w_max_ = congestion_window_;
    k_ = 0;
    in_slow_start_ = false;
}

void CubicCongestionControl::OnRttUpdated(uint64_t rtt) {
    smoothed_rtt_ = rtt;
}

size_t CubicCongestionControl::GetCongestionWindow() const {
    return congestion_window_;
}

size_t CubicCongestionControl::GetBytesInFlight() const {
    return bytes_in_flight_;
}

bool CubicCongestionControl::CanSend(size_t bytes_in_flight) const {
    return bytes_in_flight < congestion_window_;
}

uint64_t CubicCongestionControl::GetPacingRate() const {
    // Simple pacing rate calculation
    return congestion_window_ * 1000000 / smoothed_rtt_; // bytes per second
}

void CubicCongestionControl::Reset() {
    congestion_window_ = MIN_WINDOW;
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
    double w_cubic = C * tx * tx * tx + origin_point_;
    return static_cast<size_t>(w_cubic);
}

size_t CubicCongestionControl::TcpFriendlyWindowSize() {
    // TCP Reno-like window growth
    double rtt = smoothed_rtt_ / 1000000.0; // Convert to seconds
    double w_tcp = w_max_ * BETA_CUBIC + 
                  (3 * (1 - BETA_CUBIC) / (1 + BETA_CUBIC)) * 
                  (congestion_window_ / rtt);
    return static_cast<size_t>(w_tcp);
}

}
}
