#include <algorithm>
#include <cmath>

#include "common/metrics/metrics_std.h"
#include "common/qlog/qlog.h"

#include "quic/congestion_control/cubic_congestion_control.h"
#include "quic/congestion_control/normal_pacer.h"
// References for the CUBIC controller:
//   [RFC9438]      "CUBIC for Fast and Long-Distance Networks" — the
//                  authoritative spec; we cite specific sections at the
//                  decision points (W_cubic / Fast Convergence / Reno
//                  friendliness / multiplicative decrease).
//   [HyStart++]    RFC 9406 — the slow-start exit heuristic implemented
//                  by CheckHyStartExit(); used to leave slow-start
//                  *before* the first loss instead of waiting for it.
//   [RFC9002 §7]   QUIC pluggable congestion control + recovery period
//                  semantics consumed by the in_recovery_ branch in
//                  OnPacketAcked / OnPacketLost.
//   [RFC8311 §4.2] ECN-CE feedback semantics; we treat CE as an early
//                  congestion signal in the OnPacketAcked ECN branch.

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

void CubicCongestionControl::Reset() {
    // RFC 9000 §9.4 on path migration: reset to initial window. Preserve
    // bytes_in_flight_ (those packets are still unacked on the new path) and
    // the smoothed RTT (pacing needs a sane rate immediately); everything
    // else (cwnd, recovery, CUBIC epoch, HyStart) restarts from scratch.
    uint64_t bif = bytes_in_flight_;
    uint64_t srtt = srtt_us_;
    Configure(cfg_);
    bytes_in_flight_ = bif;
    srtt_us_ = srtt;
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBytesPerSec());
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

    // Initialize HyStart
    hystart_last_sent_pn_ = 0;
    app_limited_until_pn_ = 0;
    ResetHyStart();

    if (!pacer_) pacer_.reset(new NormalPacer());
}

void CubicCongestionControl::OnPacketSent(const SentPacketEvent& ev) {
    bytes_in_flight_ += ev.bytes;
    // P1-CC4: app-limited freeze — if even after this packet less than half
    // the window is in flight, the application (not the path) is the limit;
    // its ACKs must not grow the cwnd.
    if (bytes_in_flight_ < cwnd_bytes_ / 2) {
        app_limited_until_pn_ = std::max(app_limited_until_pn_, ev.pn);
    }
    // HyStart++ round delimiters (RFC 9406 §4.2): track the highest sent PN
    // so a round can close once its window end is ACKed.
    hystart_last_sent_pn_ = std::max(hystart_last_sent_pn_, ev.pn);
    if (pacer_) pacer_->OnPacketSent(ev.sent_time, static_cast<size_t>(ev.bytes));  // us: matches the pacer contract
}

void CubicCongestionControl::OnPacketAcked(const AckEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_acked) ? bytes_in_flight_ - ev.bytes_acked : 0;

    // P1-CC4: congestion events funnel through ONE entry point — the ECN-CE
    // and loss branches used to be line-for-line duplicates of the RFC 9438
    // §4.5 multiplicative decrease and had already drifted apart.
    if (ev.ecn_ce && !in_recovery_) {
        EnterRecovery(ev.ack_time);
    }

    if (in_recovery_) {
        // RFC 9002 §7.3.2: only exit recovery when ACK is for a packet sent
        // AFTER recovery_start_time_. Comparing ack_time (arrival) instead of
        // the acked packet's send time lets a single in-flight ACK exit
        // recovery prematurely and amplify cwnd collapse on subsequent loss.
        if (ev.acked_packet_send_time > recovery_start_time_us_) {
            common::CongestionStateUpdatedData qlog_data;
            qlog_data.old_state = "recovery";
            qlog_data.new_state = "congestion_avoidance";
            QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, qlog_data);
            in_recovery_ = false;
            ResetEpoch(ev.ack_time);
        } else {
            return;
        }
    }

    // P1-CC4: app-limited freeze — an ACK for a packet that left with more
    // than half the cwnd unused carries no path-capacity information.
    const bool app_limited_packet = (app_limited_until_pn_ != 0 && ev.pn <= app_limited_until_pn_);

    if (in_slow_start_ || in_css_) {
        // HyStart++ round bookkeeping runs on every ACK regardless of the
        // freeze: RTT samples are path information, not capacity votes.
        HyStartOnAck();
        // RFC 9406 §4.2: the round ends when its window-end packet is ACKed.
        if (ev.pn >= hystart_window_end_pn_) {
            HyStartOnRoundEnd();
        }

        if (!app_limited_packet) {
            if (in_css_) {
                // CSS (RFC 9406 §4.2): keep growing, but at 1/4 rate.
                cwnd_bytes_ += std::max<uint64_t>(ev.bytes_acked / kCssGrowthDivisor, 1);
            } else {
                cwnd_bytes_ += ev.bytes_acked;
            }
            cwnd_bytes_ = std::min<uint64_t>(cwnd_bytes_, cfg_.max_cwnd_bytes);
        }

        // Traditional ssthresh exit (full slow start only).
        if (in_slow_start_ && !in_css_ && cwnd_bytes_ >= ssthresh_bytes_) {
            common::CongestionStateUpdatedData qlog_data;
            qlog_data.old_state = "slow_start";
            qlog_data.new_state = "congestion_avoidance";
            QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, qlog_data);
            in_slow_start_ = false;
            ResetEpoch(ev.ack_time);
        }

        if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBytesPerSec());
        return;
    }

    if (!app_limited_packet) {
        IncreaseOnAck(ev.bytes_acked, ev.ack_time);
    }
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBytesPerSec());
}

void CubicCongestionControl::OnPacketLost(const LossEvent& ev) {
    bytes_in_flight_ = (bytes_in_flight_ > ev.bytes_lost) ? bytes_in_flight_ - ev.bytes_lost : 0;

    // Skip duplicate cwnd reduction if already in recovery
    if (in_recovery_) {
        if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBytesPerSec());
        return;
    }

    // P1-CC4: same multiplicative-decrease entry as the ECN-CE path.
    EnterRecovery(ev.lost_time);
}

void CubicCongestionControl::EnterRecovery(uint64_t now) {
    // Fast Convergence: if cwnd decreased since last loss, further reduce W_max
    // RFC 9438 §4.7 "Fast convergence": when consecutive congestion
    // events show cwnd shrinking (network getting more contended), pull
    // W_max down to cwnd * (1 + β_cubic)/2 = 0.85 * cwnd with β=0.7. This
    // gives competing flows a faster path to fairness — without it, a
    // newly-arriving CUBIC flow would see W_max anchored at the old
    // (less-contended) value and ramp up too slowly.
    double curr_w_max_pkts = BytesToPkts(cwnd_bytes_, cfg_.mss_bytes);
    if (curr_w_max_pkts < w_max_pkts_) {
        // Fast convergence: W_max = cwnd * (1 + β_cubic)/2 (RFC 9438 §4.7)
        w_max_pkts_ = curr_w_max_pkts * (1.0 + kBetaCubic) / 2.0;
    } else {
        w_max_pkts_ = curr_w_max_pkts;
    }

    // Multiplicative decrease
    // RFC 9438 §4.6: cwnd *= β_cubic (=0.7 by default). This is gentler
    // than NewReno's β=0.5 (RFC 9002 §7.3.2) — CUBIC's namesake cubic
    // probe replaces some of NewReno's window-halving conservatism with
    // explicit exploration above W_max in IncreaseOnAck below.
    uint64_t new_cwnd = static_cast<uint64_t>(cwnd_bytes_ * kBetaCubic);
    cwnd_bytes_ = std::max<uint64_t>(new_cwnd, cfg_.min_cwnd_bytes);
    ssthresh_bytes_ = cwnd_bytes_;

    {
        common::CongestionStateUpdatedData qlog_data;
        qlog_data.old_state = in_slow_start_ ? "slow_start" : "congestion_avoidance";
        qlog_data.new_state = "recovery";
        QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, qlog_data);
    }
    in_recovery_ = true;
    in_slow_start_ = false;
    // RFC 9406 §4.2: any loss or ECN event inside CSS commits straight to
    // congestion avoidance (the RTT inflation was real after all).
    in_css_ = false;
    css_rounds_ = 0;
    css_baseline_min_rtt_us_ = UINT64_MAX;
    recovery_start_time_us_ = now;
    epoch_start_us_ = 0;  // force reset on next ACK

    // Reset HyStart on congestion signal
    ResetHyStart();

    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBytesPerSec());
}

void CubicCongestionControl::OnPersistentCongestion() {
    // RFC 9002 §7.6 (+ RFC 9438 §4.7 RTO-style reset): cwnd → kMinimumWindow,
    // recovery cleared, slow start re-entered with HyStart re-armed and the
    // CUBIC epoch reset. W_max bookkeeping is kept: slow start will re-reach
    // it and the cubic probe resumes from a valid epoch.
    uint64_t old_cwnd = cwnd_bytes_;
    cwnd_bytes_ = cfg_.min_cwnd_bytes;
    in_recovery_ = false;
    recovery_start_time_us_ = 0;
    in_slow_start_ = true;
    epoch_start_us_ = 0;
    ResetHyStart();

    if (qlog_trace_) {
        common::CongestionStateUpdatedData qlog_data;
        qlog_data.old_state = "recovery";
        qlog_data.new_state = "slow_start";
        QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, qlog_data);
    }

    LOG_WARN("CubicCongestionControl::OnPersistentCongestion: cwnd: %llu->%llu (min window), re-entering slow start",
        old_cwnd, cwnd_bytes_);

    Metrics::GaugeSet(common::MetricsStd::CongestionWindowBytes, cwnd_bytes_);
    if (pacer_) pacer_->OnPacingRateUpdated(GetPacingRateBytesPerSec());
}

void CubicCongestionControl::OnRoundTripSample(
    uint64_t latest_rtt_us, uint64_t min_rtt_us, uint64_t smoothed_rtt_us, uint64_t ack_delay_us) {
    (void)min_rtt_us;
    (void)ack_delay_us;
    // P1-P4: host RttCalculator already ran the RFC 9002 5.3 EWMA - no
    // second smoothing. The raw latest sample is kept for HyStart's
    // per-round min-RTT tracking (CheckHyStartExit).
    latest_rtt_us_ = std::max<uint64_t>(latest_rtt_us, 1);
    srtt_us_ = std::max<uint64_t>(smoothed_rtt_us, 1);
}

ICongestionControl::SendState CubicCongestionControl::CanSend(uint64_t now, uint64_t& can_send_bytes) const {
    (void)now;
    uint64_t left = (cwnd_bytes_ > bytes_in_flight_) ? (cwnd_bytes_ - bytes_in_flight_) : 0;
    can_send_bytes = left;
    if (left == 0) {
        return SendState::kBlockedByCwnd;
    }
    return SendState::kOk;
}

uint64_t CubicCongestionControl::GetPacingRateBytesPerSec() const {
    // Pacing optimization: use proper initial RTT and apply pacing gain
    uint64_t rtt_us = srtt_us_;
    if (rtt_us == 0) {
        // Use QUIC default initial RTT: 333ms
        rtt_us = 333000;
    }

    // CUBIC pacing: use 1.25x gain for better burst smoothing
    // rate_bytes_per_sec = (cwnd_bytes * 1.25) / RTT_seconds
    //                   = (cwnd_bytes * 1000000 * 5) / (rtt_us * 4)
    return (cwnd_bytes_ * 1000000ull * 5) / (rtt_us * 4);
}

uint64_t CubicCongestionControl::NextSendTime(uint64_t now) const {
    if (!pacer_) return now;
    // Pacer contract is us: TimeUntilSend() returns a us delta; the caller
    // passes a us now (SendManager bridges to the ms pacing timer).
    return now + pacer_->TimeUntilSend();
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

    // RFC 9438 §4.2: W_cubic(t) = C * (t - K)^3 + W_max where K is the
    // time it takes to ramp from cwnd back to W_max under the cubic
    // curve. Below K the function is concave (slow growth around the
    // recent congestion point), above K convex (aggressive probing into
    // unexplored territory). The cube is SIGNED — never fold |t - K|:
    // the curve is point-symmetric about (K, W_max), which only holds
    // with the signed cubic. |t-K|^3 would mirror the concave half
    // ABOVE W_max, jumping cwnd past the pre-loss window on the very
    // first ACK after a loss (e.g. 100 -> 130 pkts at t=0).
    // t = (now - epoch_start)/1e6
    double t_sec = static_cast<double>(now - epoch_start_us_) / 1e6;
    double t_k = t_sec - k_time_sec_;  // signed: negative in concave region (t < K)

    // CUBIC window in packets: W_cubic(t) = C*(t - K)^3 + Wmax
    double w_cubic_pkts = kCubicC * t_k * t_k * t_k + w_max_pkts_;

    // RFC 9438 §4.3 "Reno-friendly region": on short-RTT / non-BDP-
    // limited paths, W_cubic can grow slower than what plain Reno would
    // achieve. To not lose to a competing Reno flow on those paths, we
    // also compute W_reno = W_last + 1.5 * bytes_acked / W_last
    // (per-ACK additive increase) and take the max.
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

void CubicCongestionControl::ResetHyStart() {
    // +1: windowEnd is SND.NXT (RFC 9406 §4.2) — the round closes when the
    // NEXT send position is ACKed, so the closing ACK's sample belongs to
    // the round that is ending, not to an empty new one.
    hystart_window_end_pn_ = hystart_last_sent_pn_ + 1;
    hystart_curr_round_min_rtt_us_ = UINT64_MAX;
    hystart_last_round_min_rtt_us_ = UINT64_MAX;
    hystart_rtt_sample_count_ = 0;
    in_css_ = false;
    css_rounds_ = 0;
    css_baseline_min_rtt_us_ = UINT64_MAX;
}

void CubicCongestionControl::HyStartOnAck() {
    // Below kHyStartLowWindow packets the RTT signals are noise.
    if (BytesToPkts(cwnd_bytes_, cfg_.mss_bytes) < kHyStartLowWindow) {
        return;
    }
    if (latest_rtt_us_ == 0) {
        return;  // no RTT sample attached to this ACK yet (P1-P4)
    }

    // RFC 9406 §4.2: track the current round's minimum RTT.
    hystart_curr_round_min_rtt_us_ = std::min(hystart_curr_round_min_rtt_us_, latest_rtt_us_);
    ++hystart_rtt_sample_count_;

    if (in_css_) {
        // RFC 9406 §4.2 CSS: if the RTT inflation that triggered the exit
        // turns out to be transient (this round's min came back below the
        // exit baseline), the exit was spurious — resume full slow start.
        if (hystart_curr_round_min_rtt_us_ < css_baseline_min_rtt_us_) {
            css_baseline_min_rtt_us_ = UINT64_MAX;
            in_css_ = false;
            css_rounds_ = 0;
        }
        return;
    }

    if (!in_slow_start_ || !hystart_enabled_) {
        return;
    }

    // RFC 9406 §4.2 delay increase: exit slow start (into CSS) once the
    // current round's min RTT exceeds the LAST round's min by
    // RttThresh = clamp(lastRoundMinRTT / 8, [4ms, 16ms]).
    if (hystart_rtt_sample_count_ >= kHyStartMinSamples && hystart_curr_round_min_rtt_us_ != UINT64_MAX
        && hystart_last_round_min_rtt_us_ != UINT64_MAX) {
        uint64_t rtt_thresh =
            std::max<uint64_t>(kHyStartRttThreshUs,
                std::min<uint64_t>(hystart_last_round_min_rtt_us_ / kHyStartRttDivisor, kHyStartMaxRttThreshUs));
        if (hystart_curr_round_min_rtt_us_ >= hystart_last_round_min_rtt_us_ + rtt_thresh) {
            // Enter CSS instead of jumping to congestion avoidance: under
            // crosstraffic the spike is often noise, and a hard exit here
            // is exactly the throughput cliff HyStart++ was redesigned to
            // avoid.
            css_baseline_min_rtt_us_ = hystart_curr_round_min_rtt_us_;
            in_css_ = true;
            css_rounds_ = 0;
            {
                common::CongestionStateUpdatedData qlog_data;
                qlog_data.old_state = "slow_start";
                qlog_data.new_state = "congestion_avoidance";
                QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, qlog_data);
            }
        }
    }
}

void CubicCongestionControl::HyStartOnRoundEnd() {
    // RFC 9406 §4.2: rotate the per-round minima.
    hystart_last_round_min_rtt_us_ = hystart_curr_round_min_rtt_us_;
    hystart_curr_round_min_rtt_us_ = UINT64_MAX;
    hystart_rtt_sample_count_ = 0;
    hystart_window_end_pn_ = hystart_last_sent_pn_ + 1;  // SND.NXT

    if (in_css_ && ++css_rounds_ >= kCssRounds) {
        // CSS expired: the RTT inflation persisted for CSS_ROUNDS rounds —
        // commit to congestion avoidance with ssthresh at the current cwnd.
        ssthresh_bytes_ = cwnd_bytes_;
        in_css_ = false;
        in_slow_start_ = false;
        css_baseline_min_rtt_us_ = UINT64_MAX;
        css_rounds_ = 0;
        epoch_start_us_ = 0;  // force CUBIC epoch reset on next ACK
        {
            common::CongestionStateUpdatedData qlog_data;
            qlog_data.old_state = "slow_start";
            qlog_data.new_state = "congestion_avoidance";
            QLOG_CONGESTION_STATE_UPDATED(qlog_trace_, qlog_data);
        }
    }
}

void CubicCongestionControl::SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) {
    qlog_trace_ = trace;
}

}  // namespace quic
}  // namespace quicx
