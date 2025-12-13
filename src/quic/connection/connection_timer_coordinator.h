#ifndef QUIC_CONNECTION_TIMER_COORDINATOR_H
#define QUIC_CONNECTION_TIMER_COORDINATOR_H

#include <cstdint>
#include <functional>
#include <memory>

#include "common/network/if_event_loop.h"
#include "common/timer/timer_task.h"

namespace quicx {

// Forward declaration from common namespace
namespace common {
class IEventLoop;
class TimerTask;
}  // namespace common

namespace quic {

// Forward declarations
class SendManager;
class TransportParam;
class ConnectionStateMachine;

/**
 * @brief Timer coordinator for connection-related timers
 *
 * Responsibilities:
 * - Idle timeout timer management
 * - PTO timeout checking
 * - User-defined timers
 * - Timer handling during thread transfer
 */
class TimerCoordinator {
public:
    using IdleTimeoutCallback = std::function<void()>;
    using TimerCallback = std::function<void()>;

    TimerCoordinator(std::shared_ptr<::quicx::common::IEventLoop> event_loop, TransportParam& transport_param,
        SendManager& send_manager, ConnectionStateMachine& state_machine);

    ~TimerCoordinator();

    // ==================== Idle Timeout Management ====================

    /**
     * @brief Start idle timeout timer
     * @param callback Timeout callback function
     */
    void StartIdleTimer(IdleTimeoutCallback callback);

    /**
     * @brief Reset idle timeout timer (called when data is sent/received)
     */
    void ResetIdleTimer();

    /**
     * @brief Stop idle timeout timer (called when connection closes)
     */
    void StopIdleTimer();

    // ==================== PTO Timeout Check ====================

    /**
     * @brief Check if connection should timeout due to excessive PTOs
     * RFC 9002: Close connection after persistent timeout (~3 PTO cycles)
     */
    void CheckPTOTimeout();

    // ==================== Thread Transfer Support ====================

    /**
     * @brief Prepare for thread transfer
     * Remove timers from old EventLoop
     */
    void OnThreadTransferBefore();

    /**
     * @brief Recover after thread transfer
     * Add timers to new EventLoop
     */
    void OnThreadTransferAfter();

    // ==================== User-Defined Timers ====================

    /**
     * @brief Add user-defined timer
     * @param callback Timeout callback function
     * @param timeout_ms Timeout in milliseconds
     * @return Timer ID for later removal
     */
    uint64_t AddTimer(TimerCallback callback, uint32_t timeout_ms);

    /**
     * @brief Remove user-defined timer
     * @param timer_id Timer ID to remove
     */
    void RemoveTimer(uint64_t timer_id);

private:
    // Internal idle timeout callback
    void OnIdleTimeoutInternal();

private:
    // Dependencies (injected)
    std::shared_ptr<::quicx::common::IEventLoop> event_loop_;
    TransportParam& transport_param_;
    SendManager& send_manager_;
    ConnectionStateMachine& state_machine_;

    // Idle timeout state
    ::quicx::common::TimerTask idle_timeout_task_;
    IdleTimeoutCallback idle_timeout_callback_;
    bool idle_timer_active_{false};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_TIMER_COORDINATOR_H
