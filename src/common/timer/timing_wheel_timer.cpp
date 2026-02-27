#include <limits>

#include "common/log/log.h"
#include "common/timer/timing_wheel_timer.h"
#include "common/util/time.h"

namespace quicx {
namespace common {

static constexpr uint64_t kInvalidDeadline = std::numeric_limits<uint64_t>::max();

TimingWheelTimer::TimingWheelTimer()
    : random_(0, static_cast<int32_t>(std::numeric_limits<int32_t>::max())) {}

// ---------------------------------------------------------------------------
// AddTimer
//
// O(1): copy task into wheel slot, update min cache if needed.
// ---------------------------------------------------------------------------
uint64_t TimingWheelTimer::AddTimer(TimerTask& task, uint32_t time_ms, uint64_t now) {
    uint64_t reference = (now != 0) ? now : UTCTimeMsec();

    if (!initialized_) {
        current_ms_  = reference;
        initialized_ = true;
    }

    task.id_        = static_cast<uint64_t>(random_.Random());
    task.time_      = reference + time_ms;
    task.wheel_idx_ = -1;

    Insert(task, reference);
    ++total_tasks_;

    // O(1) cache update: new deadline can only shrink the minimum.
    if (task.time_ < min_deadline_cache_) {
        min_deadline_cache_ = task.time_;
        cache_dirty_        = false;
    }

    return task.id_;
}

// ---------------------------------------------------------------------------
// RemoveTimer
//
// O(1) erase. Invalidate cache if this task held the current minimum.
// ---------------------------------------------------------------------------
bool TimingWheelTimer::RemoveTimer(TimerTask& task) {
    if (task.wheel_idx_ < 0) {
        return false;
    }

    Slot* slot = nullptr;
    switch (task.wheel_idx_) {
        case 0: slot = &wheel0_[task.slot_idx_]; break;
        case 1: slot = &wheel1_[task.slot_idx_]; break;
        case 2: slot = &wheel2_[task.slot_idx_]; break;
        case 3: slot = &overflow_;               break;
        default: return false;
    }

    slot->erase(task.list_it_);
    task.wheel_idx_ = -1;
    --total_tasks_;

    // If we removed the task that was the cached minimum, the cache is now
    // stale. Mark dirty so MinTime() will rescan on the next call.
    if (task.time_ <= min_deadline_cache_) {
        min_deadline_cache_ = kInvalidDeadline;
        cache_dirty_        = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// MinTime
//
// O(1) amortised: reads from cache unless dirty. A rescan (O(W), W=384) is
// triggered at most once per Remove or Fire that invalidated the cache.
// ---------------------------------------------------------------------------
int32_t TimingWheelTimer::MinTime(uint64_t now) {
    if (total_tasks_ == 0) {
        // Reset cache so new tasks start fresh.
        min_deadline_cache_ = kInvalidDeadline;
        cache_dirty_        = false;
        return -1;
    }
    if (now == 0) {
        now = UTCTimeMsec();
    }

    // Rebuild cache if dirty.
    if (cache_dirty_) {
        min_deadline_cache_ = EarliestDeadline();
        cache_dirty_        = false;
    }

    if (min_deadline_cache_ <= now) {
        return 0;
    }
    uint64_t diff = min_deadline_cache_ - now;
    if (diff > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        return std::numeric_limits<int32_t>::max();
    }
    return static_cast<int32_t>(diff);
}

// ---------------------------------------------------------------------------
// TimerRun
// ---------------------------------------------------------------------------
void TimingWheelTimer::TimerRun(uint64_t now) {
    if (!initialized_) {
        current_ms_ = (now != 0) ? now : UTCTimeMsec();
        initialized_ = true;
        return;
    }
    if (now == 0) {
        now = UTCTimeMsec();
    }
    if (now < current_ms_) {
        return;
    }
    Tick(now);
}

// ---------------------------------------------------------------------------
// Empty
// ---------------------------------------------------------------------------
bool TimingWheelTimer::Empty() {
    return total_tasks_ == 0;
}

// ---------------------------------------------------------------------------
// Insert (internal)
//
// Copies `task` into the appropriate slot, writes placement metadata back
// into the caller's task.
// ---------------------------------------------------------------------------
void TimingWheelTimer::Insert(TimerTask& task, uint64_t reference) {
    uint64_t deadline = task.time_;
    uint64_t delta    = (deadline > reference) ? (deadline - reference) : 0;

    auto place = [&](Slot& slot, int8_t level, uint32_t slot_idx) {
        slot.push_back(task);
        auto it        = std::prev(slot.end());
        it->wheel_idx_ = level;
        it->slot_idx_  = slot_idx;
        it->list_it_   = it;
        // Write back to caller's task for RemoveTimer.
        task.wheel_idx_ = level;
        task.slot_idx_  = slot_idx;
        task.list_it_   = it;
    };

    if (delta < kL0Range) {
        uint32_t s = static_cast<uint32_t>(deadline) & kL0Mask;
        place(wheel0_[s], 0, s);
    } else if (delta < kL1Range) {
        uint32_t s = static_cast<uint32_t>(deadline >> kL0Bits) & kL1Mask;
        place(wheel1_[s], 1, s);
    } else if (delta < kL2Range) {
        uint32_t s = static_cast<uint32_t>(deadline >> (kL0Bits + kL1Bits)) & kL2Mask;
        place(wheel2_[s], 2, s);
    } else {
        place(overflow_, 3, 0);
    }
}

// ---------------------------------------------------------------------------
// Cascade (internal)
// ---------------------------------------------------------------------------
void TimingWheelTimer::Cascade(int level, uint32_t slot) {
    Slot* src = nullptr;
    switch (level) {
        case 1: src = &wheel1_[slot]; break;
        case 2: src = &wheel2_[slot]; break;
        default: src = &overflow_;    break;
    }

    Slot local;
    local.swap(*src);

    for (TimerTask& t : local) {
        t.wheel_idx_ = -1;
        --total_tasks_;
        Insert(t, current_ms_);
        ++total_tasks_;
        // Cascaded tasks may now be in a lower level — cache is still valid
        // (their deadlines didn't change), so no dirty flag needed here.
    }
}

// ---------------------------------------------------------------------------
// Tick (internal)
//
// Fires all tasks whose deadline == current_ms_, then advances current_ms_.
// After each fired slot, if the fired deadline was the cached minimum the
// cache is marked dirty so the next MinTime() call rebuilds it efficiently.
// ---------------------------------------------------------------------------
void TimingWheelTimer::Tick(uint64_t now) {
    while (current_ms_ <= now) {
        uint32_t c0 = static_cast<uint32_t>(current_ms_) & kL0Mask;
        uint32_t c1 = static_cast<uint32_t>(current_ms_ >> kL0Bits) & kL1Mask;
        uint32_t c2 = static_cast<uint32_t>(current_ms_ >> (kL0Bits + kL1Bits)) & kL2Mask;

        // Cascade at epoch boundaries before firing.
        if (c0 == 0) {
            if (c1 == 0) {
                if (c2 == 0) {
                    Cascade(3, 0);
                }
                Cascade(2, c2);
            }
            Cascade(1, c1);
        }

        // Swap out the current L0 slot and fire all tasks in it.
        Slot fired;
        fired.swap(wheel0_[c0]);

        bool any_fired = !fired.empty();
        for (TimerTask& t : fired) {
            --total_tasks_;
            if (t.tcb_) {
                t.tcb_();
            }
        }

        // Any fired task's deadline == current_ms_. If the cached minimum
        // equals current_ms_, the minimum task just fired — invalidate.
        if (any_fired && current_ms_ <= min_deadline_cache_) {
            min_deadline_cache_ = kInvalidDeadline;
            cache_dirty_        = true;
        }

        ++current_ms_;
    }
}

// ---------------------------------------------------------------------------
// EarliestDeadline (internal) – only called when cache is dirty.
// ---------------------------------------------------------------------------
uint64_t TimingWheelTimer::EarliestDeadline() const {
    uint64_t earliest = kInvalidDeadline;

    for (const auto& slot : wheel0_) {
        for (const TimerTask& t : slot) {
            if (t.time_ < earliest) earliest = t.time_;
        }
    }
    for (const auto& slot : wheel1_) {
        for (const TimerTask& t : slot) {
            if (t.time_ < earliest) earliest = t.time_;
        }
    }
    for (const auto& slot : wheel2_) {
        for (const TimerTask& t : slot) {
            if (t.time_ < earliest) earliest = t.time_;
        }
    }
    for (const TimerTask& t : overflow_) {
        if (t.time_ < earliest) earliest = t.time_;
    }
    return earliest;
}

}  // namespace common
}  // namespace quicx
