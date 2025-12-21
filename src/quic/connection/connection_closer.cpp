#include "quic/connection/connection_closer.h"

#include "common/log/log.h"

#include "quic/connection/connection_state_machine.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/error.h"
#include "quic/connection/if_connection.h"
#include "quic/connection/transport_param.h"

namespace quicx {
namespace quic {

ConnectionCloser::ConnectionCloser(std::shared_ptr<::quicx::common::IEventLoop> event_loop,
    ConnectionStateMachine& state_machine, SendManager& send_manager, TransportParam& transport_param,
    ConnectionCloseCallback connection_close_cb):
    event_loop_(event_loop),
    state_machine_(state_machine),
    send_manager_(send_manager),
    transport_param_(transport_param),
    connection_close_cb_(connection_close_cb) {}

// ==================== Graceful Close ====================

bool ConnectionCloser::StartGracefulClose(ActiveSendCallback active_send_cb) {
    if (graceful_closing_pending_) {
        common::LOG_DEBUG("Graceful close already pending");
        return false;
    }

    // Check if there's pending data to send
    if (send_manager_.GetSendOperation() != SendOperation::kAllSendDone) {
        // Mark for graceful closing, will enter Closing state when data send completes
        common::LOG_DEBUG("Graceful close pending, waiting for data to be sent");
        graceful_closing_pending_ = true;

        // Set a timeout to force close if data doesn't complete in time
        // Use 3×PTO as a reasonable timeout (similar to draining period)
        graceful_close_timer_ = ::quicx::common::TimerTask(std::bind(&ConnectionCloser::OnGracefulCloseTimeout, this));
        event_loop_->AddTimer(graceful_close_timer_, GetCloseWaitTime() * 3, 0);
        common::LOG_DEBUG("Graceful close timeout set to %u ms", GetCloseWaitTime() * 3);
        return true;
    }

    // No pending data, can proceed immediately
    common::LOG_DEBUG("No pending data, proceeding with immediate close");

    // Store error info for retransmission
    closing_error_code_ = QuicErrorCode::kNoError;
    closing_trigger_frame_ = 0;
    closing_reason_ = "";
    last_connection_close_retransmit_time_ = 0;

    state_machine_.OnClose();
    return true;
}

bool ConnectionCloser::CheckGracefulCloseComplete(ActiveSendCallback active_send_cb) {
    if (!graceful_closing_pending_) {
        return false;
    }

    // Check if all data has been sent
    if (send_manager_.GetSendOperation() != SendOperation::kAllSendDone) {
        return false;
    }

    common::LOG_DEBUG("All data sent, proceeding with graceful close");
    graceful_closing_pending_ = false;

    // Cancel the graceful close timeout timer since we're completing normally
    event_loop_->RemoveTimer(graceful_close_timer_);

    // Enter Closing state and send CONNECTION_CLOSE
    closing_error_code_ = QuicErrorCode::kNoError;
    closing_trigger_frame_ = 0;
    closing_reason_ = "";
    last_connection_close_retransmit_time_ = 0;

    state_machine_.OnClose();
    return true;
}

void ConnectionCloser::CancelGracefulClose() {
    if (graceful_closing_pending_) {
        common::LOG_DEBUG("Canceling graceful close");
        graceful_closing_pending_ = false;
        event_loop_->RemoveTimer(graceful_close_timer_);
    }
}

void ConnectionCloser::StorePeerCloseInfo(uint64_t error, uint16_t trigger_frame, const std::string& reason) {
    closing_error_code_ = error;
    closing_trigger_frame_ = trigger_frame;
    closing_reason_ = reason;
    common::LOG_INFO(
        "Stored peer close info: error=%llu, trigger_frame=%u, reason=%s", error, trigger_frame, reason.c_str());
}

// ==================== Immediate Close ====================

void ConnectionCloser::StartImmediateClose(
    uint64_t error, uint16_t trigger_frame, const std::string& reason, ActiveSendCallback active_send_cb) {
    common::LOG_INFO("ConnectionCloser::StartImmediateClose called. error=%llu, reason=%s", error, reason.c_str());

    // Cancel graceful close if it's pending
    CancelGracefulClose();

    // Error close: enter Closing state
    // Store error info for retransmission
    closing_error_code_ = error;
    closing_trigger_frame_ = trigger_frame;
    closing_reason_ = reason;
    last_connection_close_retransmit_time_ = 0;  // Reset retransmit timer

    state_machine_.OnClose();
}

// ==================== CONNECTION_CLOSE Retransmission ====================

bool ConnectionCloser::ShouldRetransmitConnectionClose(uint64_t now) {
    // RFC 9000: Retransmit at most once per PTO to avoid flooding
    uint32_t pto = send_manager_.GetPTO(0);  // Use 0 for max_ack_delay (conservative)
    if (pto == 0) {
        pto = 100;  // Fallback to 100ms if PTO not available
    }

    // Always retransmit if last_connection_close_retransmit_time_ is 0 (first retransmit)
    // or if enough time has passed since last retransmit
    if (last_connection_close_retransmit_time_ == 0 || (now - last_connection_close_retransmit_time_) >= pto) {
        return true;
    }

    return false;
}

void ConnectionCloser::MarkConnectionCloseRetransmitted(uint64_t now) {
    last_connection_close_retransmit_time_ = now;
}

// ==================== Callback Management ====================

void ConnectionCloser::InvokeConnectionCloseCallback(
    std::shared_ptr<IConnection> connection, uint64_t error, const std::string& reason) {
    if (connection_close_cb_ && !connection_close_cb_invoked_) {
        connection_close_cb_invoked_ = true;
        common::LOG_INFO("Invoking connection close callback: error=%llu, reason=%s", error, reason.c_str());
        connection_close_cb_(connection, error, reason);
    }
}

// ==================== Timeout Management ====================

uint32_t ConnectionCloser::GetCloseWaitTime() {
    // RFC 9000: Use PTO (Probe Timeout) for connection close timing
    // PTO = smoothed_rtt + max(4*rttvar, kGranularity) + max_ack_delay
    uint32_t max_ack_delay = transport_param_.GetMaxAckDelay();
    uint32_t pto = send_manager_.GetPTO(max_ack_delay);

    // Ensure minimum timeout of 500ms for close timing
    if (pto < 500000) {  // PTO is in microseconds
        pto = 500000;
    }

    return pto / 1000;  // Convert to milliseconds for timer
}

void ConnectionCloser::OnGracefulCloseTimeout() {
    // Graceful close timeout: force entering Closing state even if data hasn't finished sending
    if (graceful_closing_pending_ && state_machine_.CanSendData()) {
        common::LOG_WARN("Graceful close timeout, forcing connection close");
        graceful_closing_pending_ = false;

        // Force enter Closing state
        closing_error_code_ = QuicErrorCode::kNoError;
        closing_trigger_frame_ = 0;
        closing_reason_ = "graceful close timeout";
        last_connection_close_retransmit_time_ = 0;

        state_machine_.OnClose();
    }
}

}  // namespace quic
}  // namespace quicx
