#include <limits>
#ifdef _MSC_VER
#include <intrin.h>
inline int __builtin_ctzll(unsigned long long mask) {
    unsigned long index;
#if defined(_WIN64)
    _BitScanForward64(&index, mask);
#else
    if ((mask & 0xffffffff) != 0) {
        _BitScanForward(&index, (unsigned long)(mask & 0xffffffff));
    } else {
        _BitScanForward(&index, (unsigned long)(mask >> 32));
        index += 32;
    }
#endif
    return index;
}
#endif


#include "common/log/log.h"
#include "common/timer/timing_wheel_timer.h"
#include "common/util/time.h"

namespace quicx {
namespace common {

static constexpr uint64_t kInvalidDeadline = std::numeric_limits<uint64_t>::max();

TimingWheelTimer::TimingWheelTimer()
    : random_(0, static_cast<int32_t>(std::numeric_limits<int32_t>::max())) {
    // Initialise per-slot min caches to "empty".
    wheel1_slot_min_.fill(kInvalidDeadline);
    wheel2_slot_min_.fill(kInvalidDeadline);
}

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

    // O(1) cache update.
    //
    // When the cache is CLEAN, we can shrink the cached minimum directly:
    // any task already in the wheel has time_ >= min_deadline_cache_, so a
    // strictly-smaller new deadline becomes the new minimum.
    //
    // When the cache is DIRTY (set kInvalidDeadline by RemoveTimer or by a
    // fired slot), there may be unscanned tasks whose deadlines are smaller
    // than this newly-inserted one. If we naively wrote
    //     min_deadline_cache_ = task.time_; cache_dirty_ = false;
    // we would wrongly advertise this new task as the minimum and skip the
    // earlier tasks still living in higher-level wheels — MinTime() would
    // then return a deadline far in the future (e.g. a 10 s idle timer
    // hiding a pending 100 ms recheck). Empirically observed: a 100 ms
    // recheck timer scheduled at t0 was reported by MinTime as 10 000 ms
    // because a subsequent ResetIdleTimer (Remove+Add 10 s) corrupted the
    // cache here. Keep dirty so the next MinTime() rescans the full wheel.
    if (!cache_dirty_ && task.time_ < min_deadline_cache_) {
        min_deadline_cache_ = task.time_;
    }

    return task.id_;
}

// ---------------------------------------------------------------------------
// RemoveTimer
//
// O(1) erase. Invalidate cache if this task held the current minimum.
// ---------------------------------------------------------------------------
bool TimingWheelTimer::RemoveTimer(TimerTask& task) {
    auto loc_it = location_map_.find(task.id_);
    if (loc_it == location_map_.end()) {
        return false;
    }

    auto it = loc_it->second;
    Slot* slot = nullptr;
    int8_t  level    = it->wheel_idx_;
    uint32_t slot_idx = it->slot_idx_;
    switch (level) {
        case 0: slot = &wheel0_[slot_idx]; break;
        case 1: slot = &wheel1_[slot_idx]; break;
        case 2: slot = &wheel2_[slot_idx]; break;
        case 3: slot = &overflow_;         break;
        default: return false;
    }

    uint64_t old_time = it->time_;
    slot->erase(it);
    location_map_.erase(loc_it);
    task.wheel_idx_ = -1;
    --total_tasks_;

    // ---- Maintain per-level occupancy bitmaps and per-slot min caches. ----
    //
    // Update strategy:
    //   * If the slot is now empty, clear its occupancy bit and reset
    //     its per-slot min to kInvalidDeadline.
    //   * If the slot still has tasks AND the removed task held the slot's
    //     min, rescan that one slot to refresh slot_min_. Cost is bounded
    //     by the slot's list length, which under steady-state QUIC traffic
    //     is small (a handful of timers).
    //   * Removing a non-min task from a slot leaves slot_min_ valid.
    switch (level) {
        case 0:
            if (slot->empty()) {
                ClrWheel0Bit(slot_idx);
            }
            break;
        case 1:
            if (slot->empty()) {
                ClrWheel1Bit(slot_idx);
                wheel1_slot_min_[slot_idx] = kInvalidDeadline;
            } else if (old_time == wheel1_slot_min_[slot_idx]) {
                wheel1_slot_min_[slot_idx] = ScanSlotMin(*slot);
            }
            break;
        case 2:
            if (slot->empty()) {
                ClrWheel2Bit(slot_idx);
                wheel2_slot_min_[slot_idx] = kInvalidDeadline;
            } else if (old_time == wheel2_slot_min_[slot_idx]) {
                wheel2_slot_min_[slot_idx] = ScanSlotMin(*slot);
            }
            break;
        case 3:
            if (slot->empty()) {
                overflow_nonempty_ = false;
                overflow_slot_min_ = kInvalidDeadline;
            } else if (old_time == overflow_slot_min_) {
                overflow_slot_min_ = ScanSlotMin(*slot);
            }
            break;
        default: break;
    }

    // If we removed the task that was the cached minimum, the cache is now
    // stale. Mark dirty so MinTime() will rescan on the next call.
    if (old_time <= min_deadline_cache_) {
        min_deadline_cache_ = kInvalidDeadline;
        cache_dirty_        = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
// MinTime
//
// O(1) amortised: reads from cache unless dirty. A rescan (now O(levels)
// thanks to the bitmap index, not O(W)) is triggered at most once per
// Remove or Fire that invalidated the cache.
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
// into the caller's task. Also maintains the level occupancy bitmap and
// per-slot min cache.
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

        location_map_[task.id_] = it;

        // Write back to caller's task for RemoveTimer.
        task.wheel_idx_ = level;
        task.slot_idx_  = slot_idx;
        task.list_it_   = it;

        // Maintain occupancy bitmap + per-slot min cache.
        switch (level) {
            case 0:
                SetWheel0Bit(slot_idx);
                break;
            case 1:
                SetWheel1Bit(slot_idx);
                if (deadline < wheel1_slot_min_[slot_idx]) {
                    wheel1_slot_min_[slot_idx] = deadline;
                }
                break;
            case 2:
                SetWheel2Bit(slot_idx);
                if (deadline < wheel2_slot_min_[slot_idx]) {
                    wheel2_slot_min_[slot_idx] = deadline;
                }
                break;
            case 3:
                overflow_nonempty_ = true;
                if (deadline < overflow_slot_min_) {
                    overflow_slot_min_ = deadline;
                }
                break;
            default: break;
        }
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

    // The source slot is now empty — clear its bitmap bit and per-slot min
    // before re-inserting; the re-inserts will re-populate the destinations.
    switch (level) {
        case 1:
            ClrWheel1Bit(slot);
            wheel1_slot_min_[slot] = kInvalidDeadline;
            break;
        case 2:
            ClrWheel2Bit(slot);
            wheel2_slot_min_[slot] = kInvalidDeadline;
            break;
        default:
            overflow_nonempty_ = false;
            overflow_slot_min_ = kInvalidDeadline;
            break;
    }

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
        if (any_fired) {
            // L0 slot is now empty: clear its occupancy bit.
            ClrWheel0Bit(c0);
        }
        for (TimerTask& t : fired) {
            --total_tasks_;
            location_map_.erase(t.id_);
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
// Bitmap scan helpers
// ---------------------------------------------------------------------------
uint32_t TimingWheelTimer::Wheel64NextSetFrom(uint64_t bm, uint32_t from) {
    if (from >= 64) return 64;
    // Mask off bits below `from`, then ctz gives the next set bit position.
    uint64_t masked = bm & (~0ull << from);
    if (masked == 0) return 64;
    return static_cast<uint32_t>(__builtin_ctzll(masked));
}

uint32_t TimingWheelTimer::Wheel0NextSetFrom(const std::array<uint64_t, 4>& bm, uint32_t from) {
    if (from >= 256) return 256;
    uint32_t word = from >> 6;          // 0..3
    uint32_t bit  = from & 63;
    // First (partial) word.
    uint64_t masked = bm[word] & (~0ull << bit);
    if (masked != 0) {
        return (word << 6) + static_cast<uint32_t>(__builtin_ctzll(masked));
    }
    // Subsequent full words.
    for (uint32_t w = word + 1; w < 4; ++w) {
        if (bm[w] != 0) {
            return (w << 6) + static_cast<uint32_t>(__builtin_ctzll(bm[w]));
        }
    }
    return 256;
}

uint64_t TimingWheelTimer::ScanSlotMin(const Slot& slot) {
    uint64_t m = kInvalidDeadline;
    for (const TimerTask& t : slot) {
        if (t.time_ < m) m = t.time_;
    }
    return m;
}

// ---------------------------------------------------------------------------
// EarliestDeadline (internal) – only called when cache is dirty.
//
// Uses the per-level occupancy bitmaps to skip empty slots in O(words),
// avoiding the previous full 384-slot scan that consumed ~20% CPU under
// load (timing_wheel_timer.cpp formerly walked every slot's std::list).
//
// Approach:
//   1. L0: scan the 256-bit occupancy bitmap starting at the current L0
//      cursor; the first set bit gives the earliest deadline directly
//      because every L0 slot represents exactly one ms.
//   2. L1/L2: the smallest L1/L2 slot index *after* the current cursor
//      contains the earliest of those slots; use the per-slot min cache
//      to read its earliest deadline without walking the slot's list.
//   3. Overflow: pick its cached min if non-empty.
//   4. Return the minimum across all levels.
// ---------------------------------------------------------------------------
uint64_t TimingWheelTimer::EarliestDeadline() const {
    uint64_t earliest = kInvalidDeadline;

    // ---- L0 ----
    // The L0 slot index for `current_ms_` is the place we'd next fire from;
    // start the scan there and wrap around.
    {
        uint32_t cursor = static_cast<uint32_t>(current_ms_) & kL0Mask;
        uint32_t s = Wheel0NextSetFrom(wheel0_occ_, cursor);
        if (s == kL0Size) {
            // No set bit at/after cursor; wrap to [0, cursor).
            s = Wheel0NextSetFrom(wheel0_occ_, 0);
            if (s < cursor) {
                // Slot belongs to the next L0 epoch — add kL0Range to the
                // base-aligned current ms to compute its absolute deadline.
                uint64_t base = (current_ms_ & ~static_cast<uint64_t>(kL0Mask)) + kL0Range;
                uint64_t cand = base + s;
                if (cand < earliest) earliest = cand;
            } else {
                // No L0 task at all (s == kL0Size).
            }
        } else {
            // Slot >= cursor in the *current* L0 epoch.
            uint64_t base = current_ms_ & ~static_cast<uint64_t>(kL0Mask);
            uint64_t cand = base + s;
            if (cand < earliest) earliest = cand;
        }
    }

    // ---- L1 ----
    // Each L1 slot represents kL0Range ms. The current L1 slot index is the
    // one whose deadlines are next to be cascaded down to L0; use the cached
    // per-slot min directly (it's already an absolute deadline).
    if (wheel1_occ_ != 0) {
        // Iterate set bits in wheel1_occ_; for each, take the slot's cached
        // min. The number of set bits is bounded by 64.
        uint64_t bm = wheel1_occ_;
        while (bm != 0) {
            uint32_t s = static_cast<uint32_t>(__builtin_ctzll(bm));
            bm &= bm - 1;  // clear lowest set bit
            uint64_t m = wheel1_slot_min_[s];
            if (m < earliest) earliest = m;
        }
    }

    // ---- L2 ----
    if (wheel2_occ_ != 0) {
        uint64_t bm = wheel2_occ_;
        while (bm != 0) {
            uint32_t s = static_cast<uint32_t>(__builtin_ctzll(bm));
            bm &= bm - 1;
            uint64_t m = wheel2_slot_min_[s];
            if (m < earliest) earliest = m;
        }
    }

    // ---- Overflow ----
    if (overflow_nonempty_ && overflow_slot_min_ < earliest) {
        earliest = overflow_slot_min_;
    }

    return earliest;
}

}  // namespace common
}  // namespace quicx
