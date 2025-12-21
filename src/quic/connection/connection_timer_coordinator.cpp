#include "common/log/log.h"
#include "common/metrics/metrics.h"
#include "common/metrics/metrics_std.h"

#include "quic/connection/connection_state_machine.h"
#include "quic/connection/connection_timer_coordinator.h"
#include "quic/connection/controler/send_manager.h"
#include "quic/connection/transport_param.h"

namespace quicx {
namespace quic {

TimerCoordinator::TimerCoordinator(std::shared_ptr<common::IEventLoop> event_loop, TransportParam& transport_param,
    SendManager& send_manager, ConnectionStateMachine& state_machine):
    event_loop_(event_loop),
    transport_param_(transport_param),
    send_manager_(send_manager),
    state_machine_(state_machine),
    idle_timer_active_(false) {
    // Initialize idle timeout task
    idle_timeout_task_.SetTimeoutCallback(std::bind(&TimerCoordinator::OnIdleTimeoutInternal, this));
}

TimerCoordinator::~TimerCoordinator() {
    StopIdleTimer();
}

// ==================== Idle Timeout Management ====================

void TimerCoordinator::StartIdleTimer(IdleTimeoutCallback callback) {
    if (!event_loop_) {
        common::LOG_ERROR("TimerCoordinator::StartIdleTimer: event_loop_ is null");
        return;
    }

    idle_timeout_callback_ = callback;

    uint32_t timeout_ms = transport_param_.GetMaxIdleTimeout();
    if (timeout_ms == 0) {
        common::LOG_WARN("TimerCoordinator::StartIdleTimer: idle timeout is 0, timer not started");
        return;
    }

    event_loop_->AddTimer(idle_timeout_task_, timeout_ms, 0);
    idle_timer_active_ = true;

    common::LOG_DEBUG("TimerCoordinator: idle timer started with timeout %u ms", timeout_ms);
}

void TimerCoordinator::ResetIdleTimer() {
    if (!idle_timer_active_) {
        return;
    }

    if (!event_loop_) {
        common::LOG_ERROR("TimerCoordinator::ResetIdleTimer: event_loop_ is null");
        return;
    }

    // Remove old timer
    event_loop_->RemoveTimer(idle_timeout_task_);

    // Add new timer
    uint32_t timeout_ms = transport_param_.GetMaxIdleTimeout();
    event_loop_->AddTimer(idle_timeout_task_, timeout_ms, 0);

    common::LOG_DEBUG("TimerCoordinator: idle timer reset");
}

void TimerCoordinator::StopIdleTimer() {
    if (!idle_timer_active_) {
        return;
    }

    if (!event_loop_) {
        common::LOG_ERROR("TimerCoordinator::StopIdleTimer: event_loop_ is null");
        return;
    }

    event_loop_->RemoveTimer(idle_timeout_task_);
    idle_timer_active_ = false;

    common::LOG_DEBUG("TimerCoordinator: idle timer stopped");
}

void TimerCoordinator::OnIdleTimeoutInternal() {
    idle_timer_active_ = false;

    // Metrics: idle timeout counter
    common::Metrics::CounterInc(common::MetricsStd::IdleTimeoutTotal);

    common::LOG_INFO("TimerCoordinator: idle timeout triggered");

    // Invoke user callback
    if (idle_timeout_callback_) {
        idle_timeout_callback_();
    }
}

// ==================== PTO Timeout Check ====================

void TimerCoordinator::CheckPTOTimeout() {
    // Only check in Connected state to avoid closing during handshake
    if (!state_machine_.CanSendData()) {
        return;
    }

    // RFC 9002: Check consecutive PTO count, if too many then consider connection dead
    uint32_t consecutive_ptos = send_manager_.GetRttCalculator().GetConsecutivePTOCount();

    // RFC 9002: Close connection after persistent timeout (~3 PTO cycles)
    if (consecutive_ptos >= RttCalculator::kMaxConsecutivePTOs) {
        common::LOG_WARN(
            "TimerCoordinator: persistent timeout detected (%u consecutive PTOs without ACK)", consecutive_ptos);

        // Metrics: PTO counter
        common::Metrics::CounterInc(common::MetricsStd::PtoCountTotal);

        // Trigger idle timeout callback (will cause connection close)
        if (idle_timeout_callback_) {
            idle_timeout_callback_();
        }
    }
}

// ==================== Thread Transfer Support ====================

void TimerCoordinator::OnThreadTransferBefore() {
    if (!event_loop_) {
        return;
    }

    // Remove idle timeout timer from old EventLoop
    if (idle_timer_active_) {
        event_loop_->RemoveTimer(idle_timeout_task_);
        common::LOG_DEBUG("TimerCoordinator: removed idle timer for thread transfer");
    }
}

void TimerCoordinator::OnThreadTransferAfter() {
    if (!event_loop_) {
        return;
    }

    // Add idle timeout timer to new EventLoop
    if (idle_timer_active_) {
        uint32_t timeout_ms = transport_param_.GetMaxIdleTimeout();
        event_loop_->AddTimer(idle_timeout_task_, timeout_ms, 0);
        common::LOG_DEBUG("TimerCoordinator: re-added idle timer after thread transfer");
    }
}

// ==================== User-Defined Timers ====================

uint64_t TimerCoordinator::AddTimer(TimerCallback callback, uint32_t timeout_ms) {
    if (!event_loop_) {
        common::LOG_ERROR("TimerCoordinator::AddTimer: event_loop_ is null");
        return 0;
    }

    uint64_t timer_id = event_loop_->AddTimer(callback, timeout_ms);
    common::LOG_DEBUG("TimerCoordinator: added user timer %llu with timeout %u ms", timer_id, timeout_ms);

    return timer_id;
}

void TimerCoordinator::RemoveTimer(uint64_t timer_id) {
    if (!event_loop_) {
        common::LOG_ERROR("TimerCoordinator::RemoveTimer: event_loop_ is null");
        return;
    }

    event_loop_->RemoveTimer(timer_id);
    common::LOG_DEBUG("TimerCoordinator: removed user timer %llu", timer_id);
}

}  // namespace quic
}  // namespace quicx
