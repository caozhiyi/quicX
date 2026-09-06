#include <quicx/common/metrics.h>

#include "common/log/log.h"
#include "common/metrics/metrics_std.h"
#include "common/util/time.h"

#include "quic/config.h"
#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/controller/send_manager.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/type.h"

namespace quicx {
namespace quic {

SendManager::SendManager(std::shared_ptr<common::ITimerScheduler> scheduler):
    send_control_(scheduler),
    packet_number_(),
    send_flow_controller_(nullptr),
    scheduler_(scheduler) {
    send_control_.SetPacketLostCallback([this](std::shared_ptr<IPacket> packet) {
        LOG_WARN("SendManager: packet %llu lost, triggering retransmission sc=%p", packet->GetPacketNumber(),
            (void*)&send_control_);
        // Note: send_retry_cb_ (which calls BaseConnection::ActiveSend) will check connection state
        // and ignore the callback if connection is closing/draining/closed
        if (send_retry_cb_) {
            send_retry_cb_();
        }
    });
}

SendManager::~SendManager() {}

void SendManager::UpdateConfig(const TransportParam& tp) {
    send_control_.UpdateConfig(tp);
}

SendOperation SendManager::GetSendOperation() {
    // Check if there are frames or active streams to send
    bool has_active_data = !wait_frame_list_.empty();
    if (stream_manager_) {
        has_active_data = has_active_data || stream_manager_->HasActiveStreams();
    }

    if (!has_active_data) {
        // PERF VALIDATION: record yields. A high steady-state rate here while
        // we still expect bulk transfer means the send loop is repeatedly
        // emptying the queue faster than the application is feeding it —
        // i.e. application-limited rather than network-limited.
        Metrics::CounterInc(common::MetricsStd::DiagSendAllDone);
        return SendOperation::kAllSendDone;

    } else {
        // Use the PMTU prober's current effective MTU rather than a hard-coded
        // 1500: the prober starts at the RFC 9000 §14.1 floor (1200) on a new
        // path and probes upwards (up to 1500) only after success. This makes
        // CanSend() honour the discovered path MTU instead of optimistically
        // assuming wire-MTU on every connection.
        uint64_t can_send_size = pmtu_prober_.GetMtuLimit();
        uint64_t now = common::UTCTimeMsec();
        send_control_.CanSend(now, can_send_size);
        if (can_send_size == 0) {
            // RFC 9002: Allow ACK-only packets to bypass congestion control.
            // Also allow path-validation probes (PATH_CHALLENGE/RESPONSE) to
            // bypass: after a NAT rebind the entire cwnd is in flight towards
            // the dead mapping, and requiring the queue to be 100% exempt lets
            // ONE lingering non-exempt frame (e.g. RETIRE_CONNECTION_ID from a
            // DCID rotation) park the connection for the whole 5 s probe
            // window — the challenge then never leaves and validation fails
            // 5/5 on every rebind (observed). Safety: downstream filters
            // (GetPendingFrames' IsAllowedOnUnvalidated skip when
            // !streams_allowed_, TrySendNewBurst's probing bypass) ensure only
            // the tiny exempt frames actually go out under cwnd exhaustion.
            if (!IsCongestionControlExempt() && !HasPendingProbingFrame()) {
                uint64_t next_time = send_control_.GetNextSendTime(now);
                if (next_time > now) {
                    uint64_t delay = next_time - now;
                    ArmPacingTimer(static_cast<uint32_t>(delay));
                } else {
                    is_cwnd_limited_ = true;
                    LOG_WARN("congestion control send data limited.");
                }
                // PERF VALIDATION: this branch covers both pacing-throttled
                // and cwnd-exhausted yields. Distinguishing them in the dump
                // would require a second counter; in practice on loopback
                // pacing rarely fires so this is effectively cwnd_blocked.
                Metrics::CounterInc(common::MetricsStd::DiagSendBlockedCwnd);
                return SendOperation::kNextPeriod;
            }
        }
    }
    is_cwnd_limited_ = false;
    // PERF VALIDATION: success path. Per-second rate roughly equals "packets
    // per second the worker is allowed to attempt"; cross-check against
    // pkts_tx in the same dump line — a large gap means TrySend itself is
    // failing later (e.g. PacketBuilder produced an empty payload).
    Metrics::CounterInc(common::MetricsStd::DiagSendImmediateOk);
    return SendOperation::kSendAgainImmediately;
}

void SendManager::EnqueueFrame(std::shared_ptr<IFrame> frame) {
    wait_frame_list_.emplace_front(frame);
}

bool SendManager::HasPendingProbingFrame() const {
    for (const auto& frame : wait_frame_list_) {
        uint16_t type = static_cast<uint16_t>(frame->GetType());
        if (type == FrameType::kPathChallenge || type == FrameType::kPathResponse) {
            return true;
        }
    }
    return false;
}

void SendManager::OnPacketAck(PacketNumberSpace ns, std::shared_ptr<IFrame> frame) {
    // Pass to send control for RTT/loss/cc updates
    send_control_.OnPacketAck(common::UTCTimeMsec(), ns, frame);

    // Bug #17: an incoming ACK is also a wake-up signal for a flow-control-
    // blocked connection. The peer might bundle MAX_DATA with the ACK, and
    // even if it doesn't, the ACK can free congestion-window room or expose
    // newly-lost packets that need retransmission. In all cases we must let
    // BaseConnection::TrySend re-evaluate; without this hook the connection
    // would stay parked outside the worker's active set until idle timeout.
    bool need_resume = is_cwnd_limited_ || is_flow_control_blocked_;
    if (is_cwnd_limited_) {
        is_cwnd_limited_ = false;
        LOG_INFO("SendManager::OnPacketAck: clearing is_cwnd_limited_");
    }
    if (is_flow_control_blocked_) {
        // Don't clear unconditionally — TrySend will re-set the flag (and the
        // recheck timer) if flow control is still active. But we must give it
        // a chance to retry by issuing the retry callback below.
        is_flow_control_blocked_ = false;
        LOG_INFO("SendManager::OnPacketAck: clearing is_flow_control_blocked_, will let TrySend re-evaluate");
    }
    if (need_resume) {
        if (send_retry_cb_) {
            send_retry_cb_();
            LOG_INFO("SendManager::OnPacketAck: send_retry_cb_ executed");
        } else {
            LOG_WARN("SendManager::OnPacketAck: send_retry_cb_ is null!");
        }
    }

    // PMTU probe success detection: delegate to PmtuProber
    if (pmtu_prober_.CheckAckCoversProbe(frame)) {
        return;
    }
}

void SendManager::ResetPathSignals() {
    // RFC 9000 §9.4 on path migration: old-path congestion state (cwnd,
    // recovery epoch, pacing rate, capacity estimates) is invalid on the new
    // path. After a NAT-rebind blackout the surviving cwnd is massively
    // oversized for the fresh path: loss-marking releases in-flight budget,
    // the retransmit drain refills it, and the result is a multi-MB
    // retransmit flood that overflowed the sim bottleneck (~182 MB sent for a
    // 10 MB file, ~90% dropped, contiguous delivery stuck at the first hole).
    // This used to be a stub that only touched UpdateConfig(); actually reset
    // the controller now. bytes_in_flight is preserved inside Reset().
    send_control_.ResetCongestionControl();

    // Reset the RTT estimator: old-path SRTT drives PTO/loss detection on the
    // new path and is typically wrong there. Note UpdateConfig() is NOT
    // called (the pre-fix stub did, with a default TransportParam): it would
    // clobber the NEGOTIATED max_ack_delay / ack_delay_exponent with defaults,
    // and nothing re-delivers the real values after a migration.
    send_control_.ResetRtt();
}

void SendManager::ResetMtuForNewPath() {
    pmtu_prober_.ResetForNewPath();
}

void SendManager::ClearActiveStreams() {
    if (stream_manager_) {
        stream_manager_->ClearActiveStreams();
    }
    wait_frame_list_.clear();
    send_control_.ClearRetransmissionData();
    // Bug #17: connection is closing — disarm the flow-control recheck so the
    // timer wheel's pending callback does not fire on a teardowning connection.
    is_flow_control_blocked_ = false;
    if (flow_control_recheck_scheduled_) {
        flow_control_recheck_timer_.Cancel();
        flow_control_recheck_scheduled_ = false;
    }
}

void SendManager::ClearRetransmissionData() {
    send_control_.ClearRetransmissionData();
}

bool SendManager::CheckAndChargeAmpBudget(uint32_t bytes) {
    // Keyed off the controller's own state rather than streams_allowed_. The two
    // are distinct concerns: streams_allowed_ gates stream *scheduling* during path
    // validation, while the amplification budget applies to every datagram,
    // including the handshake ones sent long before any stream exists. Gating the
    // budget on streams_allowed_ (as this previously did) silently disabled the
    // RFC 9000 §8.1 limit for the entire handshake.
    //
    // TryCharge() atomically checks AND debits in a single CAS, so concurrent
    // datagram emissions cannot both pass the check and over-debit past the 3x
    // budget (the old CanSend()+OnBytesSent() pair had a TOCTOU window under
    // kMultiThread).
    if (amp_controller_.TryCharge(bytes)) {
        return true;
    }
    amp_blocked_ = true;
    LOG_WARN("anti-amplification budget exhausted. want:%u, remaining:%llu, recv:%llu, sent:%llu", bytes,
        (unsigned long long)amp_controller_.GetRemainingBudget(),
        (unsigned long long)amp_controller_.GetBytesReceived(), (unsigned long long)amp_controller_.GetBytesSent());
    return false;
}

void SendManager::MarkAddressValidated() {
    if (!amp_controller_.IsUnvalidated()) {
        return;
    }
    amp_controller_.ExitUnvalidatedState();
    amp_blocked_ = false;
    streams_allowed_ = true;
    LOG_DEBUG("peer address validated; anti-amplification limit lifted.");
}

bool SendManager::IsAllowedOnUnvalidated(uint16_t type) const {
    if (streams_allowed_) {
        return true;
    }
    return IsExemptFrameType(type);
}

// static
bool SendManager::IsExemptFrameType(uint16_t type) {
    switch (type) {
        case FrameType::kPathChallenge:
        case FrameType::kPathResponse:
        case FrameType::kAck:
        case FrameType::kAckEcn:
        case FrameType::kPing:
        case FrameType::kPadding:
        case FrameType::kCrypto:
        case FrameType::kNewConnectionId:
        case FrameType::kRetireConnectionId:
        case FrameType::kConnectionClose:
        case FrameType::kConnectionCloseApp:
            return true;
        default:
            break;
    }
    return false;
}

void SendManager::ResetAmpBudget(uint64_t initial_credit) {
    amp_controller_.EnterUnvalidatedState(initial_credit);
    amp_blocked_ = false;
}

void SendManager::OnCandidatePathBytesReceived(uint32_t bytes) {
    if (!amp_controller_.IsUnvalidated()) {
        return;
    }
    amp_controller_.OnBytesReceived(bytes);

    // Fresh credit arrived: let the connection retry the send it had to drop,
    // otherwise a datagram refused by the budget would never be re-attempted and
    // the handshake would stall until idle timeout.
    if (amp_blocked_) {
        amp_blocked_ = false;
        if (send_retry_cb_) {
            send_retry_cb_();
        }
    }
}

bool SendManager::ShouldSendRetry() const {
    if (streams_allowed_) {
        return false;
    }
    return amp_controller_.IsNearLimit();
}

void SendManager::StartMtuProbe() {
    pmtu_prober_.StartProbe();
}

void SendManager::OnMtuProbeResult(bool success) {
    pmtu_prober_.OnProbeResult(success);
}

// RFC 9002: Check if frames are exempt from congestion control (ACKs, CONNECTION_CLOSE)
bool SendManager::IsCongestionControlExempt() const {
    // Don't check active streams - even if streams are waiting, we should send ACKs/Close first
    // Only check the wait_frame_list_ for what's immediately pending

    if (wait_frame_list_.empty()) {
        return false;
    }

    // Check if all pending frames are exempt (ACKs or CONNECTION_CLOSE)
    for (const auto& frame : wait_frame_list_) {
        auto frame_type = frame->GetType();
        if (frame_type == FrameType::kAck || frame_type == FrameType::kAckEcn ||
            frame_type == FrameType::kConnectionClose || frame_type == FrameType::kConnectionCloseApp) {
            continue;
        }
        // Path-validation probes (PATH_CHALLENGE / PATH_RESPONSE) and PTO PING
        // are congestion-control exempt as an engineering trade-off (no
        // precise RFC clause): after a NAT rebind the whole cwnd window is in
        // flight towards the dead mapping, so cwnd is exhausted for multiple
        // RTTs; without this exemption GetSendOperation() parks the connection
        // (kNextPeriod) and the worker never calls TrySendBurst — the
        // challenge queued by the PathManager could then NEVER leave
        // (observed: probes failing 5/5 on every rebind while only the PTO
        // timer's direct retransmit path, which bypasses this gate, kept
        // hitting the amp limit).
        if (frame_type == FrameType::kPathChallenge || frame_type == FrameType::kPathResponse ||
            frame_type == FrameType::kPing) {
            continue;
        }
        return false;
    }

    return true;
}

void SendManager::SetQlogTrace(std::shared_ptr<common::QlogTrace> trace) {
    qlog_trace_ = trace;
    send_control_.SetQlogTrace(trace);
}

uint32_t SendManager::GetAvailableWindow() {
    uint64_t can_send_size = pmtu_prober_.GetMtuLimit();
    uint64_t now = common::UTCTimeMsec();
    send_control_.CanSend(now, can_send_size);

    return static_cast<uint32_t>(can_send_size);
}

void SendManager::SetCwndLimited() {
    is_cwnd_limited_ = true;
}

void SendManager::SetFlowControlBlocked() {
    is_flow_control_blocked_ = true;
    // PERF VALIDATION: this fires from BaseConnection::TrySend (conn-level
    // FC) and StreamManager (every active stream blocked on STREAM_DATA).
    // A non-zero rate here during stable transfer is the smoking gun for
    // "throughput is governed by peer's flow-control window-extension cadence
    // rather than CPU or network". We deliberately collapse both call sites
    // into one counter for dashboard simplicity.
    Metrics::CounterInc(common::MetricsStd::DiagFlowControlBlocked);
    if (!flow_control_recheck_scheduled_ && scheduler_) {
        flow_control_recheck_scheduled_ = true;
        ArmFlowControlRecheckTimer();
    }
}

void SendManager::ArmPacingTimer(uint32_t delay_ms) {
    if (pacing_timer_.Rearm(delay_ms)) {
        return;
    }
    if (!scheduler_) {
        return;
    }
    pacing_timer_ = scheduler_->AddTimer(
        life_token_,
        [this]() {
            if (send_retry_cb_) {
                send_retry_cb_();
            }
        },
        delay_ms);
}

void SendManager::ArmFlowControlRecheckTimer() {
    // Bug #17: low-frequency wake-up while the connection is held back by the
    // peer's connection-level flow control limit. Without this fallback, a
    // connection that has buffered stream data but cannot send (peer's max_data
    // exhausted, all in-flight packets already acked, peer not forthcoming with
    // MAX_DATA) gets removed from the worker's active set and never re-examined
    // until idle timeout fires.
    if (flow_control_recheck_timer_.Rearm(kFlowControlRecheckIntervalMs)) {
        return;
    }
    if (!scheduler_) {
        return;
    }
    flow_control_recheck_timer_ = scheduler_->AddTimer(
        life_token_,
        [this]() {
            flow_control_recheck_scheduled_ = false;
            if (!is_flow_control_blocked_) {
                return;  // already unblocked via ACK / MAX_DATA
            }
            LOG_INFO("SendManager: flow-control recheck timer fired, retrying send");
            if (send_retry_cb_) {
                send_retry_cb_();
            }
        },
        kFlowControlRecheckIntervalMs);
}

std::vector<std::shared_ptr<IFrame>> SendManager::GetPendingFrames(
    EncryptionLevel level, uint32_t max_bytes, bool exempt_only) {
    std::vector<std::shared_ptr<IFrame>> result;
    uint32_t total_bytes = 0;

    // Iterate through wait_frame_list_ and collect frames suitable for this encryption level
    for (auto iter = wait_frame_list_.begin(); iter != wait_frame_list_.end();) {
        auto frame = *iter;
        uint32_t frame_size = frame->EncodeSize();

        // Check if adding this frame would exceed max_bytes
        if (total_bytes + frame_size > max_bytes) {
            break;
        }

        // Check if frame is suitable for this encryption level
        if (level != EncryptionLevel::kApplication) {
            FrameType type = static_cast<FrameType>(frame->GetType());
            bool allowed = false;
            // RFC 9000 Section 12.1: Packet Protection
            // Initial/Handshake only allow ACK, CRYPTO, PADDING, PING, CONNECTION_CLOSE
            if (type == FrameType::kAck || type == FrameType::kAckEcn || type == FrameType::kCrypto ||
                type == FrameType::kConnectionClose || type == FrameType::kPadding || type == FrameType::kPing) {
                allowed = true;
            }

            if (!allowed) {
                ++iter;
                continue;
            }
        }

        // Restricted egress windows: while the path is unvalidated (probe in
        // flight, RFC 9000 §8.1/§8.2 amplification) or while the congested-
        // path probing bypass is active (exempt_only), only frames on the
        // exemption list may leave. Retransmitted stream frames used to be
        // packed into the same datagram as the PATH_CHALLENGE, inflating it
        // past the budget so the whole datagram (challenge included) was
        // dropped at the emitter — validation could then never complete.
        // Keep such frames queued; they are flushed once the window clears.
        if ((!streams_allowed_ || exempt_only) && !IsExemptFrameType(frame->GetType())) {
            ++iter;
            continue;
        }

        if (static_cast<FrameType>(frame->GetType()) == FrameType::kPathChallenge) {
            LOG_DEBUG(
                "SendManager: PATH_CHALLENGE dispatched to burst builder (streams_allowed=%d, exempt_only=%d, "
                "max_bytes=%u)",
                streams_allowed_ ? 1 : 0, exempt_only ? 1 : 0, max_bytes);
        }
        result.push_back(frame);
        total_bytes += frame_size;
        iter = wait_frame_list_.erase(iter);  // Remove from pending list
    }

    return result;
}

bool SendManager::HasStreamData(EncryptionLevel level) {
    if (!stream_manager_) {
        return false;
    }
    return stream_manager_->HasActiveStreamsForLevel(level);
}

}  // namespace quic
}  // namespace quicx
