#include <algorithm>
#include "quic/congestion_control/normal_pacer.h"
#include "quic/congestion_control/reno_congestion_control.h"

namespace quicx {
namespace quic {

RenoCongestionControl::RenoCongestionControl() {
    Configure({});
}

void RenoCongestionControl::Configure(const CcConfigV2& cfg) {
    cfg_ = cfg;
    cwnd_bytes_ = cfg_.initial_cwnd_bytes;
    ssthresh_bytes_ = UINT64_MAX;
    bytes_in_flight_ = 0;
    in_slow_start_ = true;
    in_recovery_ = false;
    recovery_start_time_ = 0;
    if (!pacer_) {
        pacer_.reset(new NormalPacer());
    }
}

void RenoCongestionControl::OnPacketSent(const SentPacketEvent& ev) {
    bytes_in_flight_ += ev.bytes;
    if (pacer_) {
        pacer_->OnPacketSent(ev.sent_time, static_cast<size_t>(ev.bytes));
    }
}

void RenoCongestionControl::OnPacketAcked(const AckEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_acked) ? bytes_in_flight_ - ev.bytes_acked : 0;
    if (in_recovery_) {
        if (ev.ack_time > recovery_start_time_) {
            in_recovery_ = false;
        } else {
            return;
        }
    }
    IncreaseOnAck(ev.bytes_acked);
    UpdatePacingRate();
}

void RenoCongestionControl::OnPacketLost(const LossEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_lost) ? bytes_in_flight_ - ev.bytes_lost : 0;
    if (!in_recovery_) {
        EnterRecovery(ev.lost_time);
    }
    UpdatePacingRate();
}

void RenoCongestionControl::OnRoundTripSample(uint64_t latest_rtt, uint64_t ack_delay) {
    (void)ack_delay;
    if (srtt_us_ == 0) {
        srtt_us_ = latest_rtt;
    }
    srtt_us_ = (7 * srtt_us_ + latest_rtt) / 8;
}

RenoCongestionControl::SendState RenoCongestionControl::CanSend(uint64_t now, uint64_t& can_send_bytes) const {
    (void)now;
    uint64_t left = (cwnd_bytes_ > bytes_in_flight_) ? (cwnd_bytes_ - bytes_in_flight_) : 0;
    can_send_bytes = left;
    if (left == 0) {
        return SendState::kBlockedByCwnd;
    }
    return SendState::kOk;
}

uint64_t RenoCongestionControl::GetPacingRateBps() const {
    if (srtt_us_ == 0) {
        return cfg_.min_cwnd_bytes * 8;
    }
    return (cwnd_bytes_ * 8ull * 1000000ull) / srtt_us_;
}

uint64_t RenoCongestionControl::NextSendTime(uint64_t now) const {
    (void)now;
    if (!pacer_) {
        return 0;
    }
    return pacer_->TimeUntilSend();
}

void RenoCongestionControl::IncreaseOnAck(uint64_t bytes_acked) {
    if (in_slow_start_) {
        cwnd_bytes_ += bytes_acked;
        if (cwnd_bytes_ >= ssthresh_bytes_) {
            in_slow_start_ = false;
        }
    } else {
        // Reno: cwnd += MSS*MSS / cwnd per ACK aggregate; approximate via cfg_.mss_bytes
        uint64_t add = (cfg_.mss_bytes * cfg_.mss_bytes) / std::max<uint64_t>(cwnd_bytes_, 1);
        cwnd_bytes_ += std::max<uint64_t>(add, 1);
    }
    cwnd_bytes_ = std::min<uint64_t>(cwnd_bytes_, cfg_.max_cwnd_bytes);
}

void RenoCongestionControl::EnterRecovery(uint64_t now) {
    ssthresh_bytes_ = static_cast<uint64_t>(cwnd_bytes_ * cfg_.beta);
    cwnd_bytes_ = std::max<uint64_t>(cfg_.min_cwnd_bytes, ssthresh_bytes_);
    in_recovery_ = true;
    recovery_start_time_ = now;
    in_slow_start_ = false;
}

void RenoCongestionControl::UpdatePacingRate() {
    if (!pacer_) {
        return;
    }
    pacer_->OnPacingRateUpdated(GetPacingRateBps());
}

} // namespace quic
} // namespace quicx


