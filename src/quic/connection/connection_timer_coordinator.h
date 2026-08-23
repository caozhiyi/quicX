#ifndef QUIC_CONNECTION_TIMER_COORDINATOR_H
#define QUIC_CONNECTION_TIMER_COORDINATOR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

#include <quicx/common/if_event_loop.h>
#include <quicx/common/if_timer_scheduler.h>

namespace quicx {

// Forward declaration from common namespace
namespace common {
class IEventLoop;
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

    // ==================== Keep-Alive ====================

    /**
     * @brief Start periodic keep-alive timer (client-side, RFC 9000 §10.1.2)
     *
     * A download-only client stops emitting packets the moment the server's
     * path to it breaks (e.g. a NAT rebind): no received data means no ACKs,
     * so the peer never observes the new network address and both sides idle
     * out. A periodic PING keeps traffic flowing through the CURRENT mapping,
     * which both refreshes the peer's idle timer and reveals address changes.
     *
     * Interval is max(idle_timeout/2, kMinKeepAliveMs) so the probe always
     * lands well before the idle timer expires.
     *
     * @param callback Invoked every interval while the connection can send
     */
    void StartKeepAliveTimer(TimerCallback callback);

    /**
     * @brief Stop the keep-alive timer (called when connection closes)
     */
    void StopKeepAliveTimer();

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
    uint64_t AddTimer(TimerCallback callback, uint32_t timeout_ms, bool periodic = false);

    /**
     * @brief Remove user-defined timer
     * @param timer_id Timer ID to remove
     */
    void RemoveTimer(uint64_t timer_id);

private:
    // Internal idle timeout callback
    void OnIdleTimeoutInternal();

    // (Re-)arm the keep-alive timer on the given loop using the stored
    // callback and interval. Shared by StartKeepAliveTimer and
    // OnThreadTransferAfter.
    void ArmKeepAliveTimer(const std::shared_ptr<common::IEventLoop>& loop);

private:
    // Dependencies (injected)
    std::weak_ptr<::quicx::common::IEventLoop> event_loop_;
    TransportParam& transport_param_;
    SendManager& send_manager_;
    ConnectionStateMachine& state_machine_;

    // Guards every callback we register: it expires with `*this`, so a firing
    // that races with our destruction is skipped instead of calling into a dead
    // object. The handles below already cancel on destruction; this covers the
    // one case a cancel cannot (a callback that has already started).
    std::shared_ptr<int> life_token_ = std::make_shared<int>(0);

    // Idle timeout state. The handle is the timer: destroying or reassigning it
    // cancels, from any thread. That replaces the previous
    //IsInLoopThread() ? RemoveTimer(task) : RunInLoop(remove by id)
    // dance which appeared three times in this file and lost the timer id on the
    // cross-thread path (the reinstalled task got a fresh id that died with the
    // lambda, so Stop/~TimerCoordinator could never cancel it again -- and its
    // callback captured a raw `this`).
    ::quicx::common::Timer idle_timer_;
    IdleTimeoutCallback idle_timeout_callback_;
    bool idle_timer_active_{false};

    // Keep-alive state. Same lifetime rules as the idle timer above, plus the
    // callback/interval needed to re-arm after a thread transfer.
    ::quicx::common::Timer keep_alive_timer_;
    bool keep_alive_active_{false};
    TimerCallback keep_alive_callback_;
    uint32_t keep_alive_interval_ms_{0};

    // User timers keep the uint64_t id contract of IQuicConnection::AddTimer,
    // so the id is a local counter and the handle lives here.
    std::unordered_map<uint64_t, ::quicx::common::Timer> user_timers_;
    uint64_t next_user_timer_id_{1};
};

}  // namespace quic
}  // namespace quicx

#endif  // QUIC_CONNECTION_TIMER_COORDINATOR_H
