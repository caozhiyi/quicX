#include <algorithm>

#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"
#include "common/qlog/qlog.h"

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
    uint64_t old_bytes_in_flight = bytes_in_flight_;
    bytes_in_flight_ += ev.bytes;
    common::LOG_DEBUG(
        "RenoCongestionControl::OnPacketSent: pn=%llu, bytes=%llu, bytes_in_flight: %llu->%llu, cwnd=%llu", ev.pn,
        ev.bytes, old_bytes_in_flight, bytes_in_flight_, cwnd_bytes_);

    // Metrics: Bytes in flight
    common::Metrics::GaugeSet(common::MetricsStd::BytesInFlight, bytes_in_flight_);

    if (pacer_) {
        pacer_->OnPacketSent(ev.sent_time, static_cast<size_t>(ev.bytes));
    }
}

void RenoCongestionControl::OnPacketAcked(const AckEvent& ev) {
    uint64_t old_bytes_in_flight = bytes_in_flight_;
    uint64_t old_cwnd = cwnd_bytes_;

    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_acked) ? bytes_in_flight_ - ev.bytes_acked : 0;

    // Metrics: Bytes in flight
    common::Metrics::GaugeSet(common::MetricsStd::BytesInFlight, bytes_in_flight_);

    common::LOG_DEBUG(
        "RenoCongestionControl::OnPacketAcked: pn=%llu, bytes_acked=%llu, bytes_in_flight: %llu->%llu, cwnd=%llu",
        ev.pn, ev.bytes_acked, old_bytes_in_flight, bytes_in_flight_, cwnd_bytes_);

    // Treat ECN-CE as a congestion signal similar to loss (RFC3168 behavior)
    if (ev.ecn_ce) {
        if (!in_recovery_) {
            EnterRecovery(ev.ack_time);
        }
        UpdatePacingRate();
        return;
    }
    if (in_recovery_) {
        if (ev.ack_time > recovery_start_time_) {
            // Log congestion state transition: recovery -> congestion_avoidance
            if (qlog_trace_) {
                common::CongestionStateUpdatedData data;
                data.old_state = "recovery";
                data.new_state = "congestion_avoidance";
                QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, data);
            }
            in_recovery_ = false;
        } else {
            return;
        }
    }
    IncreaseOnAck(ev.bytes_acked);

    common::LOG_DEBUG("RenoCongestionControl::OnPacketAcked: cwnd: %llu->%llu, in_slow_start=%d", old_cwnd, cwnd_bytes_,
        in_slow_start_);

    UpdatePacingRate();
}

void RenoCongestionControl::OnPacketLost(const LossEvent& ev) {
    uint64_t old_bytes_in_flight = bytes_in_flight_;
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_lost) ? bytes_in_flight_ - ev.bytes_lost : 0;

    // Metrics: Bytes in flight
    common::Metrics::GaugeSet(common::MetricsStd::BytesInFlight, bytes_in_flight_);

    common::LOG_WARN(
        "RenoCongestionControl::OnPacketLost: pn=%llu, bytes_lost=%llu, bytes_in_flight: %llu->%llu, cwnd=%llu", ev.pn,
        ev.bytes_lost, old_bytes_in_flight, bytes_in_flight_, cwnd_bytes_);
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

ICongestionControl::SendState RenoCongestionControl::CanSend(uint64_t now, uint64_t& can_send_bytes) const {
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
            // Log congestion state transition: slow_start -> congestion_avoidance
            if (qlog_trace_) {
                common::CongestionStateUpdatedData data;
                data.old_state = "slow_start";
                data.new_state = "congestion_avoidance";
                QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, data);
            }
            in_slow_start_ = false;
        }
    } else {
        // Reno: cwnd += MSS*MSS / cwnd per ACK aggregate; approximate via cfg_.mss_bytes
        uint64_t add = (cfg_.mss_bytes * cfg_.mss_bytes) / std::max<uint64_t>(cwnd_bytes_, 1);
        cwnd_bytes_ += std::max<uint64_t>(add, 1);
    }
    cwnd_bytes_ = std::min<uint64_t>(cwnd_bytes_, cfg_.max_cwnd_bytes);

    // Metrics: Congestion window updated
    common::Metrics::GaugeSet(common::MetricsStd::CongestionWindowBytes, cwnd_bytes_);
}

void RenoCongestionControl::EnterRecovery(uint64_t now) {
    uint64_t old_cwnd = cwnd_bytes_;
    uint64_t old_ssthresh = ssthresh_bytes_;

    // Log congestion state transition: -> recovery
    if (qlog_trace_) {
        common::CongestionStateUpdatedData data;
        data.old_state = in_slow_start_ ? "slow_start" : "congestion_avoidance";
        data.new_state = "recovery";
        QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, data);
    }

    ssthresh_bytes_ = static_cast<uint64_t>(cwnd_bytes_ * cfg_.beta);
    cwnd_bytes_ = std::max<uint64_t>(cfg_.min_cwnd_bytes, ssthresh_bytes_);
    if (cwnd_bytes_ < bytes_in_flight_) {
        cwnd_bytes_ = bytes_in_flight_;
    }
    in_recovery_ = true;
    recovery_start_time_ = now;
    in_slow_start_ = false;

    common::LOG_WARN(
        "RenoCongestionControl::EnterRecovery: cwnd: %llu->%llu, ssthresh: %llu->%llu, bytes_in_flight=%llu", old_cwnd,
        cwnd_bytes_, old_ssthresh, ssthresh_bytes_, bytes_in_flight_);

    // Metrics: Congestion event
    common::Metrics::CounterInc(common::MetricsStd::CongestionEventsTotal);
    common::Metrics::GaugeSet(common::MetricsStd::CongestionWindowBytes, cwnd_bytes_);

    // Metrics: Slow start exit
    if (old_cwnd != cwnd_bytes_ && in_slow_start_ != (cwnd_bytes_ < ssthresh_bytes_)) {
        common::Metrics::CounterInc(common::MetricsStd::SlowStartExits);
    }
}

void RenoCongestionControl::UpdatePacingRate() {
    if (!pacer_) {
        return;
    }
    uint64_t pacing_rate = GetPacingRateBps();
    pacer_->OnPacingRateUpdated(pacing_rate);

    // Metrics: Pacing rate
    common::Metrics::GaugeSet(common::MetricsStd::PacingRateBytesPerSec, pacing_rate / 8);
}

void RenoCongestionControl::SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) {
    qlog_trace_ = trace;
}

}  // namespace quic
}  // namespace quicx
