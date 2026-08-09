#include <quicx/common/metrics.h>
#include <quicx/common/metrics_std.h>
#include "common/log/log.h"

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
    idle_timer_active_(false) {}

TimerCoordinator::~TimerCoordinator() {
    // Nothing to do: every timer we own is held by a Timer handle, and
    // ~Timer() cancels -- synchronously when we are on the owning loop thread,
    // by queueing the cancel for that thread otherwise.
    //
    // This used to be 25 lines of cross-thread choreography, because connections
    // are legitimately destroyed off the loop thread (~QuicServer on the
    // application thread, worker_map_.clear() during teardown) and calling
    // EventLoop::RemoveTimer() from there trips AssertInLoopThread() and aborts.
    // The choreography removed the timer "by id, captured by value" -- which
    // silently stopped working once ResetIdleTimer had rewritten that id (see
    // the note in ResetIdleTimer), leaving the idle timer armed with a callback
    // that held a raw pointer to this object.
}

// ==================== Idle Timeout Management ====================

void TimerCoordinator::StartIdleTimer(IdleTimeoutCallback callback) {
    auto loop = event_loop_.lock();
    if (!loop) {
        LOG_ERROR("TimerCoordinator::StartIdleTimer: event_loop_ expired");
        return;
    }

    idle_timeout_callback_ = callback;

    uint32_t timeout_ms = static_cast<uint32_t>(transport_param_.GetMaxIdleTimeout());
    if (timeout_ms == 0) {
        LOG_WARN("TimerCoordinator::StartIdleTimer: idle timeout is 0, timer not started");
        return;
    }

    idle_timer_ = loop->AddTimer(life_token_, [this]() { OnIdleTimeoutInternal(); }, timeout_ms);
    idle_timer_active_ = true;

    LOG_DEBUG("TimerCoordinator: idle timer started with timeout %u ms", timeout_ms);
}

void TimerCoordinator::ResetIdleTimer() {
    if (!idle_timer_active_) {
        return;
    }

    auto loop = event_loop_.lock();
    if (!loop) {
        LOG_ERROR("TimerCoordinator::ResetIdleTimer: event_loop_ expired");
        return;
    }

    uint32_t timeout_ms = static_cast<uint32_t>(transport_param_.GetMaxIdleTimeout());

    // Steady state: one packet in or out per call, so this is a hot path. Rearm
    // splices the existing node to its new slot -- no allocation, no re-copy of
    // the callback, and the handle stays the same object.
    //
    // The close paths (handshake watchdog -> CloseInternal -> send -> idle reset,
    // master-thread teardown) can reach this from a foreign thread, and the wheel
    // is single-threaded, so those hand the rearm to the loop. What they must NOT
    // do is what the previous code did: re-register a *fresh* TimerTask carrying a
    // hand-set id. AddTimer overwrites the id it is given, so the new registration
    // ended up with an id nobody outside that lambda knew, and this object's copy
    // of the id went stale -- after which StopIdleTimer() and the destructor could
    // no longer cancel the idle timer at all, leaving a callback holding a raw
    // `this` armed on a connection about to be freed.
    if (loop->IsInLoopThread()) {
        idle_timer_.Rearm(timeout_ms);
    } else {
        std::weak_ptr<int> token = life_token_;
        loop->RunInLoop([this, token, timeout_ms]() {
            // Runs on the loop thread. If we were destroyed meanwhile the token
            // has expired and idle_timer_ is gone along with its timer.
            if (token.expired()) {
                return;
            }
            idle_timer_.Rearm(timeout_ms);
        });
    }

    LOG_DEBUG("TimerCoordinator: idle timer reset");
}

void TimerCoordinator::StopIdleTimer() {
    if (!idle_timer_active_) {
        return;
    }

    // Cancel() is safe from any thread by contract, so the previous
    // IsInLoopThread()/RunInLoop split is gone.
    idle_timer_.Cancel();
    idle_timer_active_ = false;

    LOG_DEBUG("TimerCoordinator: idle timer stopped");
}

void TimerCoordinator::OnIdleTimeoutInternal() {
    idle_timer_active_ = false;

    // Metrics: idle timeout counter
    common::Metrics::CounterInc(common::MetricsStd::IdleTimeoutTotal);

    LOG_INFO("TimerCoordinator: idle timeout triggered");

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
        LOG_WARN("TimerCoordinator: persistent timeout detected (%u consecutive PTOs without ACK)", consecutive_ptos);

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
    // Cancel is thread-safe, and the transfer runs while the connection is
    // detached from its loop, so there is nothing to post.
    if (idle_timer_active_) {
        idle_timer_.Cancel();
        LOG_DEBUG("TimerCoordinator: removed idle timer for thread transfer");
    }
}

void TimerCoordinator::OnThreadTransferAfter() {
    auto loop = event_loop_.lock();
    if (!loop) {
        return;
    }

    // Re-arm idle timeout timer on the (possibly new) EventLoop
    if (idle_timer_active_) {
        uint32_t timeout_ms = static_cast<uint32_t>(transport_param_.GetMaxIdleTimeout());
        idle_timer_ = loop->AddTimer(life_token_, [this]() { OnIdleTimeoutInternal(); }, timeout_ms);
        LOG_DEBUG("TimerCoordinator: re-added idle timer after thread transfer");
    }
}

// ==================== User-Defined Timers ====================

uint64_t TimerCoordinator::AddTimer(TimerCallback callback, uint32_t timeout_ms, bool periodic) {
    auto loop = event_loop_.lock();
    if (!loop) {
        LOG_ERROR("TimerCoordinator::AddTimer: event_loop_ expired");
        return 0;
    }

    // IQuicConnection::AddTimer hands users a uint64_t, so the id is minted here
    // and the handle is kept alongside it. The wrapper drops its own entry once
    // it has run, which is what keeps this map bounded -- the EventLoop-side
    // equivalent (timer_ids_) grew with every timer ever armed.
    uint64_t id = next_user_timer_id_++;
    if (periodic) {
        // Re-arm is handled by the wheel (interval != 0), so the wrapper must
        // NOT drop its entry after the first fire -- doing so would destroy the
        // Timer handle and cancel the underlying node. The id stays until an
        // explicit RemoveTimer or the coordinator's life token expires.
        user_timers_[id] = loop->AddRepeatTimer(life_token_, [this, id, callback]() {
            (void)id;
            if (callback) {
                callback();
            }
        }, timeout_ms);
    } else {
        user_timers_[id] = loop->AddTimer(life_token_, [this, id, callback]() {
            if (callback) {
                callback();
            }
            user_timers_.erase(id);
        }, timeout_ms);
    }

    return id;
}

void TimerCoordinator::RemoveTimer(uint64_t timer_id) {
    // Erasing the handle cancels the timer and releases the closure.
    user_timers_.erase(timer_id);
    LOG_DEBUG("TimerCoordinator: removed user timer %llu", (unsigned long long)timer_id);
}

}  // namespace quic
}  // namespace quicx
