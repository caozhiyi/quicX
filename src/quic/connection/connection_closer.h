#ifndef QUIC_CONNECTION_CLOSER_H
#define QUIC_CONNECTION_CLOSER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "common/network/if_event_loop.h"

namespace quicx {
namespace quic {

// Forward declarations
class IConnection;
class ConnectionStateMachine;
class SendManager;
class TransportParam;

/**
 * @brief Connection closer for managing connection termination
 *
 * Responsibilities:
 * - Graceful connection close (wait for data to be sent)
 * - Immediate connection close (error scenarios)
 * - CONNECTION_CLOSE frame generation and retransmission
 * - Closing/Draining state timeout management
 */
class ConnectionCloser {
public:
    using ConnectionCloseCallback =
        std::function<void(std::shared_ptr<IConnection>, uint64_t error, const std::string& reason)>;
    using ActiveSendCallback = std::function<void()>;

    ConnectionCloser(std::shared_ptr<::quicx::common::IEventLoop> event_loop, ConnectionStateMachine& state_machine,
        SendManager& send_manager, TransportParam& transport_param, ConnectionCloseCallback connection_close_cb);

    ~ConnectionCloser() = default;

    // ==================== Graceful Close ====================

    /**
     * @brief Initiate graceful close (wait for pending data to be sent)
     * @param active_send_cb Callback to trigger sending
     * @return true if closing started, false if already closing
     */
    bool StartGracefulClose(ActiveSendCallback active_send_cb);

    /**
     * @brief Check if graceful close can proceed (all data sent)
     * @param active_send_cb Callback to trigger sending CONNECTION_CLOSE
     * @return true if close proceeded, false if still waiting
     */
    bool CheckGracefulCloseComplete(ActiveSendCallback active_send_cb);

    /**
     * @brief Cancel graceful close (peer initiated close or error occurred)
     */
    void CancelGracefulClose();

    /**
     * @brief Store peer's CONNECTION_CLOSE error info
     * @param error Error code from peer
     * @param trigger_frame Frame type that triggered close
     * @param reason Error reason from peer
     */
    void StorePeerCloseInfo(uint64_t error, uint16_t trigger_frame, const std::string& reason);

    // ==================== Immediate Close ====================

    /**
     * @brief Initiate immediate close with error
     * @param error Error code
     * @param trigger_frame Frame type that triggered close
     * @param reason Error reason string
     * @param active_send_cb Callback to trigger sending CONNECTION_CLOSE
     */
    void StartImmediateClose(
        uint64_t error, uint16_t trigger_frame, const std::string& reason, ActiveSendCallback active_send_cb);

    // ==================== CONNECTION_CLOSE Retransmission ====================

    /**
     * @brief Check if CONNECTION_CLOSE should be retransmitted
     * @param now Current timestamp (milliseconds)
     * @return true if retransmission is needed
     */
    bool ShouldRetransmitConnectionClose(uint64_t now);

    /**
     * @brief Mark CONNECTION_CLOSE as retransmitted
     * @param now Current timestamp (milliseconds)
     */
    void MarkConnectionCloseRetransmitted(uint64_t now);

    // ==================== State Queries ====================

    /**
     * @brief Get stored error code for CONNECTION_CLOSE retransmission
     */
    uint64_t GetClosingErrorCode() const { return closing_error_code_; }

    /**
     * @brief Get stored trigger frame type for CONNECTION_CLOSE
     */
    uint16_t GetClosingTriggerFrame() const { return closing_trigger_frame_; }

    /**
     * @brief Get stored reason string for CONNECTION_CLOSE
     */
    const std::string& GetClosingReason() const { return closing_reason_; }

    /**
     * @brief Check if graceful close is pending
     */
    bool IsGracefulClosePending() const { return graceful_closing_pending_; }

    /**
     * @brief Check if connection close callback has been invoked
     */
    bool IsConnectionCloseCallbackInvoked() const { return connection_close_cb_invoked_; }

    /**
     * @brief Mark connection close callback as invoked (prevents duplicate calls)
     */
    void MarkConnectionCloseCallbackInvoked() { connection_close_cb_invoked_ = true; }

    /**
     * @brief Invoke connection close callback
     * @param connection Connection instance
     * @param error Error code
     * @param reason Error reason
     */
    void InvokeConnectionCloseCallback(
        std::shared_ptr<IConnection> connection, uint64_t error, const std::string& reason);

    // ==================== Timeout Management ====================

    /**
     * @brief Calculate close wait time (draining period)
     * @return Wait time in milliseconds (3 × PTO)
     */
    uint32_t GetCloseWaitTime();

private:
    // Timeout handler for graceful close
    void OnGracefulCloseTimeout();

    // Dependencies (injected)
    std::shared_ptr<::quicx::common::IEventLoop> event_loop_;
    ConnectionStateMachine& state_machine_;
    SendManager& send_manager_;
    TransportParam& transport_param_;
    ConnectionCloseCallback connection_close_cb_;

    // Error info for CONNECTION_CLOSE retransmission
    uint64_t closing_error_code_{0};
    uint16_t closing_trigger_frame_{0};
    std::string closing_reason_;
    uint64_t last_connection_close_retransmit_time_{0};

    // Graceful close state
    bool graceful_closing_pending_{false};
    ::quicx::common::TimerTask graceful_close_timer_;

    // Track callback invocation to prevent duplicates
    bool connection_close_cb_invoked_{false};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_CLOSER_H
