#include <algorithm>
#include <vector>

#include "common/log/log.h"
#include "common/util/time.h"

#include "quic/connection/controller/retransmit_scheduler.h"

namespace quicx {
namespace quic {

RetransmitScheduler::RetransmitScheduler(
    std::shared_ptr<common::ITimerScheduler> scheduler, Clock now, IntervalProvider base_pto):
    scheduler_(std::move(scheduler)),
    now_(now ? std::move(now) : Clock([] { return common::MonotonicTimeMsec(); })),
    base_pto_(std::move(base_pto)) {}

RetransmitScheduler::~RetransmitScheduler() {
    timer_.Cancel();
}

uint64_t RetransmitScheduler::IntervalMs() const {
    // >= 1 ms: a zero interval would let a fired slot re-fire in the same pass.
    const uint64_t v = base_pto_ ? base_pto_() : 0;
    return std::max<uint64_t>(v, 1);
}

void RetransmitScheduler::InsertOrReplace(IRetransmitOwner* owner, uint32_t slot, uint64_t deadline_ms) {
    const Key key{owner, slot};
    auto it = index_.find(key);
    if (it != index_.end()) {
        queue_.erase(Entry{it->second, owner, slot});
        it->second = deadline_ms;
    } else {
        index_.emplace(key, deadline_ms);
    }
    queue_.insert(Entry{deadline_ms, owner, slot});
}

void RetransmitScheduler::Arm(IRetransmitOwner* owner, uint32_t slot) {
    if (!owner || index_.count(Key{owner, slot}) != 0) {
        return;
    }
    InsertOrReplace(owner, slot, now_() + IntervalMs());
    if (!dispatching_) {
        Reschedule();
    }
}

void RetransmitScheduler::Restart(IRetransmitOwner* owner, uint32_t slot) {
    Update(owner, slot, now_() + IntervalMs());
}

void RetransmitScheduler::Update(IRetransmitOwner* owner, uint32_t slot, uint64_t deadline_ms) {
    if (!owner) {
        return;
    }
    InsertOrReplace(owner, slot, deadline_ms);
    if (!dispatching_) {
        Reschedule();
    }
}

void RetransmitScheduler::Remove(IRetransmitOwner* owner, uint32_t slot) {
    auto it = index_.find(Key{owner, slot});
    if (it == index_.end()) {
        return;
    }
    queue_.erase(Entry{it->second, owner, slot});
    index_.erase(it);
    if (!dispatching_) {
        Reschedule();
    }
}

void RetransmitScheduler::RemoveOwner(IRetransmitOwner* owner) {
    bool changed = false;
    for (auto it = index_.lower_bound(Key{owner, 0}); it != index_.end() && it->first.first == owner;) {
        queue_.erase(Entry{it->second, owner, it->first.second});
        it = index_.erase(it);
        changed = true;
    }
    if (changed && !dispatching_) {
        Reschedule();
    }
}

void RetransmitScheduler::Clear() {
    queue_.clear();
    index_.clear();
    timer_.Cancel();
}

bool RetransmitScheduler::IsArmed(const IRetransmitOwner* owner, uint32_t slot) const {
    return index_.count(Key{const_cast<IRetransmitOwner*>(owner), slot}) != 0;
}

void RetransmitScheduler::RunDue(uint64_t now) {
    // Bound the pass by the entries present at its start: every fired slot is
    // re-registered at now + interval (> now), so a well-formed pass visits
    // each due slot once; the bound is a belt against a misbehaving clock.
    size_t budget = queue_.size();
    const uint64_t interval = IntervalMs();
    dispatching_ = true;
    while (budget-- > 0 && !queue_.empty() && queue_.begin()->deadline_ms <= now) {
        const Entry due = *queue_.begin();
        // Restart the slot BEFORE the callback: the cadence is arithmetic from
        // the fire instant, and the handler may still override via Update()
        // or drop the slot via Remove().
        InsertOrReplace(due.owner, due.slot, now + interval);
        due.owner->OnRetransmitDeadline(due.slot, now);
    }
    dispatching_ = false;
    Reschedule();
}

void RetransmitScheduler::SetLifeToken(std::weak_ptr<void> life) {
    life_ = std::move(life);
    if (!dispatching_) {
        Reschedule();
    }
}

void RetransmitScheduler::Reschedule() {
    if (queue_.empty()) {
        timer_.Cancel();
        return;
    }
    if (!scheduler_ || life_.expired()) {
        return;  // nothing to arm with, or the owner guard is not bound yet
    }
    const uint64_t now = now_();
    const uint64_t deadline = queue_.begin()->deadline_ms;
    const uint32_t delay = static_cast<uint32_t>(deadline > now ? std::min<uint64_t>(deadline - now, UINT32_MAX) : 0);
    // Rearm keeps the node and the handle (no allocation per update); it only
    // fails when there is no armed timer yet, then a fresh one is created.
    if (timer_.Rearm(delay, now)) {
        return;
    }
    timer_ = scheduler_->AddTimer(life_, [this]() { RunDue(now_()); }, delay);
}

}  // namespace quic
}  // namespace quicx
