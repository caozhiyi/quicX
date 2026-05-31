#include "common/log/log.h"
#include "common/util/time.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>

#include "quic/connection/connection_stream_manager.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/crypto/tls/type.h"
#include "quic/frame/ack_frame.h"
#include "quic/frame/type.h"

namespace quicx {
namespace quic {

SendManager::SendManager(std::shared_ptr<common::ITimer> timer):
    send_control_(timer),
    send_flow_controller_(nullptr),
    packet_number_(),
    timer_(timer) {
    pacing_timer_task_ = common::TimerTask();
    pacing_timer_task_.SetTimeoutCallback([this]() {
        if (send_retry_cb_) {
            send_retry_cb_();
        }
    });

    // Bug #17: low-frequency wake-up while the connection is held back by the
    // peer's connection-level flow control limit. Without this fallback, a
    // connection that has buffered stream data but cannot send (peer's
    // max_data exhausted, all in-flight packets already acked, peer not
    // forthcoming with MAX_DATA) gets removed from the worker's active set
    // and never re-examined until idle timeout fires.
    flow_control_recheck_task_ = common::TimerTask();
    flow_control_recheck_task_.SetTimeoutCallback([this]() {
        flow_control_recheck_scheduled_ = false;
        if (!is_flow_control_blocked_) {
            return;  // already unblocked via ACK / MAX_DATA
        }
        LOG_INFO("SendManager: flow-control recheck timer fired, retrying send");
        if (send_retry_cb_) {
            send_retry_cb_();
        }
    });

    send_control_.SetPacketLostCallback([this](std::shared_ptr<IPacket> packet) {
        LOG_WARN("SendManager: packet %llu lost, triggering retransmission", packet->GetPacketNumber());
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
        common::Metrics::CounterInc(common::MetricsStd::DiagSendAllDone);
        return SendOperation::kAllSendDone;

    } else {
        uint64_t can_send_size = 1500;  // TODO: set to mtu size
        uint64_t now = common::UTCTimeMsec();
        send_control_.CanSend(now, can_send_size);
        if (can_send_size == 0) {
            // RFC 9002: Allow ACK-only packets to bypass congestion control
            if (!IsCongestionControlExempt()) {
                uint64_t next_time = send_control_.GetNextSendTime(now);
                if (next_time > now) {
                    uint64_t delay = next_time - now;
                    timer_->AddTimer(pacing_timer_task_, delay);
                } else {
                    is_cwnd_limited_ = true;
                    LOG_WARN("congestion control send data limited.");
                }
                // PERF VALIDATION: this branch covers both pacing-throttled
                // and cwnd-exhausted yields. Distinguishing them in the dump
                // would require a second counter; in practice on loopback
                // pacing rarely fires so this is effectively cwnd_blocked.
                common::Metrics::CounterInc(common::MetricsStd::DiagSendBlockedCwnd);
                return SendOperation::kNextPeriod;
            }
        }
    }
    is_cwnd_limited_ = false;
    // PERF VALIDATION: success path. Per-second rate roughly equals "packets
    // per second the worker is allowed to attempt"; cross-check against
    // pkts_tx in the same dump line — a large gap means TrySend itself is
    // failing later (e.g. PacketBuilder produced an empty payload).
    common::Metrics::CounterInc(common::MetricsStd::DiagSendImmediateOk);
    return SendOperation::kSendAgainImmediately;
}

void SendManager::ToSendFrame(std::shared_ptr<IFrame> frame) {
    wait_frame_list_.emplace_front(frame);
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
    // Recreate congestion controller with default config and reset RTT estimator via UpdateConfig
    CcConfigV2 cfg;  // defaults
    // Reconfigure congestion control
    // The current implementation doesn't expose Configure directly; rebuild via factory path in SendControl
    // So we simulate by resetting RTT to initial via UpdateConfig and relying on controller startup behavior
    TransportParam dummy;
    send_control_.UpdateConfig(dummy);
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
    if (flow_control_recheck_scheduled_ && timer_) {
        timer_->RemoveTimer(flow_control_recheck_task_);
        flow_control_recheck_scheduled_ = false;
    }
}

void SendManager::ClearRetransmissionData() {
    send_control_.ClearRetransmissionData();
}

bool SendManager::CheckAndChargeAmpBudget(uint32_t bytes) {
    if (!streams_allowed_) {
        if (!amp_controller_.CanSend(bytes)) {
            return false;
        }
        amp_controller_.OnBytesSent(bytes);
        return true;
    }
    // When streams are allowed, path is validated; disable amp limit
    return true;
}

bool SendManager::IsAllowedOnUnvalidated(uint16_t type) const {
    if (streams_allowed_) {
        return true;
    }
    switch (type) {
        case FrameType::kPathChallenge:
        case FrameType::kPathResponse:
        case FrameType::kAck:
        case FrameType::kAckEcn:
        case FrameType::kPing:
        case FrameType::kPadding:
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

void SendManager::ResetAmpBudget() {
    amp_controller_.EnterUnvalidatedState();
}

void SendManager::OnCandidatePathBytesReceived(uint32_t bytes) {
    if (!streams_allowed_) {
        amp_controller_.OnBytesReceived(bytes);
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
        } else {
            return false;
        }
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
    common::Metrics::CounterInc(common::MetricsStd::DiagFlowControlBlocked);
    static constexpr uint32_t kFlowControlRecheckIntervalMs = 100;
    if (!flow_control_recheck_scheduled_ && timer_) {
        flow_control_recheck_scheduled_ = true;
        timer_->AddTimer(flow_control_recheck_task_, kFlowControlRecheckIntervalMs);
    }
}

std::vector<std::shared_ptr<IFrame>> SendManager::GetPendingFrames(EncryptionLevel level, uint32_t max_bytes) {
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