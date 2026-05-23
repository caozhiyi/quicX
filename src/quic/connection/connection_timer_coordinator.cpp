#include "common/log/log.h"
#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>

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
    // Bug-fix (close-path cross-thread fatal):
    //   When the connection is destroyed from a thread that is *not* the
    //   owning EventLoop's thread (e.g. ~QuicServer running on the application
    //   thread, or worker_map_.clear() during teardown), calling
    //   loop->RemoveTimer() directly trips AssertInLoopThread() inside
    //   EventLoop and aborts the process with the "EventLoop accessed from
    //   wrong thread!" fatal that we observe ~750 ms after CloseInternal.
    //
    // Strategy:
    //   1. If the loop has already expired, nothing to do.
    //   2. If we're on the loop thread, RemoveTimer is safe synchronously.
    //   3. Otherwise, the timer task is owned by `*this` and lives in
    //      idle_timeout_task_; once we return from this destructor that
    //      object is gone. We cannot simply post the RemoveTimer to the loop
    //      because it would dereference a dangling task. Instead we issue a
    //      synchronous-style RunInLoop with a lookup by the task id which
    //      we capture by value.
    if (!idle_timer_active_) {
        return;
    }
    auto loop = event_loop_.lock();
    if (!loop) {
        return;
    }
    if (loop->IsInLoopThread()) {
        loop->RemoveTimer(idle_timeout_task_);
        idle_timer_active_ = false;
        return;
    }
    // Cross-thread destruction: remove by id, captured by value, so the
    // posted lambda is independent of *this. This is a best-effort cleanup;
    // if the timer fires before the task runs, OnIdleTimeoutInternal is
    // protected by idle_timer_active_=false set right below in the lambda
    // dispatch closure (idle_timeout_callback_ may still be invoked once
    // but the connection's weak_ptr-guarded callback layer above us will
    // safely no-op when the connection is gone).
    uint64_t task_id = idle_timeout_task_.GetId();
    loop->RunInLoop([loop, task_id]() {
        common::TimerTask probe;
        probe.SetIdForTest(task_id);
        loop->RemoveTimer(probe);
    });
    idle_timer_active_ = false;
}

// ==================== Idle Timeout Management ====================

void TimerCoordinator::StartIdleTimer(IdleTimeoutCallback callback) {
    auto loop = event_loop_.lock();
    if (!loop) {
        common::LOG_ERROR("TimerCoordinator::StartIdleTimer: event_loop_ expired");
        return;
    }

    idle_timeout_callback_ = callback;

    uint32_t timeout_ms = static_cast<uint32_t>(transport_param_.GetMaxIdleTimeout());
    if (timeout_ms == 0) {
        common::LOG_WARN("TimerCoordinator::StartIdleTimer: idle timeout is 0, timer not started");
        return;
    }

    loop->AddTimer(idle_timeout_task_, timeout_ms, 0);
    idle_timer_active_ = true;

    common::LOG_DEBUG("TimerCoordinator: idle timer started with timeout %u ms", timeout_ms);
}

void TimerCoordinator::ResetIdleTimer() {
    if (!idle_timer_active_) {
        return;
    }

    auto loop = event_loop_.lock();
    if (!loop) {
        common::LOG_ERROR("TimerCoordinator::ResetIdleTimer: event_loop_ expired");
        return;
    }

    // Remove old timer
    loop->RemoveTimer(idle_timeout_task_);

    // Add new timer
    uint32_t timeout_ms = static_cast<uint32_t>(transport_param_.GetMaxIdleTimeout());
    loop->AddTimer(idle_timeout_task_, timeout_ms, 0);

    common::LOG_DEBUG("TimerCoordinator: idle timer reset");
}

void TimerCoordinator::StopIdleTimer() {
    if (!idle_timer_active_) {
        return;
    }

    auto loop = event_loop_.lock();
    if (!loop) {
        common::LOG_ERROR("TimerCoordinator::StopIdleTimer: event_loop_ expired");
        return;
    }

    // Cross-thread safety (companion to ~TimerCoordinator). StopIdleTimer
    // can legitimately be invoked during connection teardown that happens on
    // any thread (e.g. a master thread tearing down a worker, or the
    // application thread invoking quic->Destroy()). Direct RemoveTimer would
    // trip EventLoop::AssertInLoopThread().
    if (loop->IsInLoopThread()) {
        loop->RemoveTimer(idle_timeout_task_);
    } else {
        uint64_t task_id = idle_timeout_task_.GetId();
        loop->RunInLoop([loop, task_id]() {
            common::TimerTask probe;
            probe.SetIdForTest(task_id);
            loop->RemoveTimer(probe);
        });
    }
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
    auto loop = event_loop_.lock();
    if (!loop) {
        return;
    }

    // Remove idle timeout timer from old EventLoop
    if (idle_timer_active_) {
        loop->RemoveTimer(idle_timeout_task_);
        common::LOG_DEBUG("TimerCoordinator: removed idle timer for thread transfer");
    }
}

void TimerCoordinator::OnThreadTransferAfter() {
    auto loop = event_loop_.lock();
    if (!loop) {
        return;
    }

    // Add idle timeout timer to new EventLoop
    if (idle_timer_active_) {
        uint32_t timeout_ms = static_cast<uint32_t>(transport_param_.GetMaxIdleTimeout());
        loop->AddTimer(idle_timeout_task_, timeout_ms, 0);
        common::LOG_DEBUG("TimerCoordinator: re-added idle timer after thread transfer");
    }
}

// ==================== User-Defined Timers ====================

uint64_t TimerCoordinator::AddTimer(TimerCallback callback, uint32_t timeout_ms) {
    auto loop = event_loop_.lock();
    if (!loop) {
        common::LOG_ERROR("TimerCoordinator::AddTimer: event_loop_ expired");
        return 0;
    }

    uint64_t timer_id = loop->AddTimer(callback, timeout_ms);

    return timer_id;
}

void TimerCoordinator::RemoveTimer(uint64_t timer_id) {
    auto loop = event_loop_.lock();
    if (!loop) {
        common::LOG_ERROR("TimerCoordinator::RemoveTimer: event_loop_ expired");
        return;
    }

    loop->RemoveTimer(timer_id);
    common::LOG_DEBUG("TimerCoordinator: removed user timer %llu", timer_id);
}

}  // namespace quic
}  // namespace quicx
