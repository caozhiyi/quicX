#include <quicx/common/if_timer_scheduler.h>

#include "common/timer/timer_core.h"
#include "common/util/time.h"

namespace quicx {
namespace common {

// Implementation of the public Timer handle declared in
// include/quicx/common/if_timer_scheduler.h. It is kept out of line so the
// public header never needs the definition of TimerCore.

Timer::Timer(std::shared_ptr<TimerCore> core, uint32_t index, uint32_t gen) noexcept
    : core_(std::move(core)), index_(index), gen_(gen) {}

Timer::~Timer() {
    Cancel();
}

Timer::Timer(Timer&& other) noexcept: core_(other.core_), index_(other.index_), gen_(other.gen_) {
    other.core_.reset();
}

Timer& Timer::operator=(Timer&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    // Assigning over a live handle must cancel the timer it owned, otherwise the
    // old timer would leak and still fire.
    Cancel();
    core_ = other.core_;
    index_ = other.index_;
    gen_ = other.gen_;
    other.core_.reset();
    return *this;
}

bool Timer::IsActive() const noexcept {
    auto core = core_.lock();
    return core && core->IsActive(index_, gen_);
}

void Timer::Cancel() noexcept {
    auto core = core_.lock();
    if (!core) {
        // Core already torn down (e.g. the owning EventLoop was destroyed before
        // this handle was cancelled). Nothing to do and nothing to dereference --
        // this is what makes destruction/cancellation from any thread safe.
        core_.reset();
        return;
    }
    if (core->IsInLoopThread()) {
        // Fast path: unlink the node and release the slot right away, so the
        // callback (and anything it captured) is destroyed immediately.
        core->CancelLocal(index_, gen_);
        core->ReleaseEntry(index_, gen_);
    } else {
        // Teardown path: hand the cancel to the loop thread. It is applied
        // before the loop runs any further callback, so the contract "no
        // callback after Cancel() returns" still holds for callbacks that have
        // not started yet. If the loop is already gone the weak_ptr above would
        // have returned null and we would not reach here.
        core->CancelRemote(index_, gen_);
    }
    // Detach unconditionally: a handle never refers to a slot it does not own,
    // which is what makes double-cancel and use-after-cancel safe no-ops.
    core_.reset();
}

bool Timer::Rearm(uint32_t delay_ms, uint64_t now) noexcept {
    auto core = core_.lock();
    if (!core) {
        return false;
    }
    // Rearm reuses the same slot and generation, so the handle stays valid and
    // keeps referring to the timer it already owned. Do NOT detach here.
    return core->Rearm(index_, gen_, delay_ms, now != 0 ? now : UTCTimeMsec());
}

}  // namespace common
}  // namespace quicx
