#ifdef _MSC_VER
#include <intrin.h>
static inline int __builtin_ctzll(unsigned long long mask) {
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
    return (int)index;
}
#endif

#include "common/timer/timer_core.h"

namespace quicx {
namespace common {

namespace {
constexpr uint64_t kInvalidDeadline = std::numeric_limits<uint64_t>::max();
}  // namespace

// ---------------------------------------------------------------------------
// slab management
// ---------------------------------------------------------------------------

uint32_t TimerCore::AllocEntry() {
    if (free_head_ != kNoEntry) {
        uint32_t index = free_head_;
        free_head_ = slab_[index].next_free;
        Entry& entry = slab_[index];
        entry.next_free = kNoEntry;
        entry.in_use = true;
        entry.has_node = false;
        entry.level = -1;
        entry.slot = 0;
        return index;
    }

    if (slab_.size() >= static_cast<size_t>(kNoEntry)) {
        return kNoEntry;
    }
    slab_.emplace_back();
    uint32_t index = static_cast<uint32_t>(slab_.size() - 1);
    slab_[index].in_use = true;
    return index;
}

TimerCore::Slot& TimerCore::ListOf(int8_t level, uint32_t slot) {
    switch (level) {
        case 0:
            return wheel0_[slot];
        case 1:
            return wheel1_[slot];
        case 2:
            return wheel2_[slot];
        case 3:
            return overflow_;
        default:
            return idle_;
    }
}

void TimerCore::ComputeDest(uint64_t deadline, uint64_t reference, int8_t& level, uint32_t& slot) const {
    uint64_t delta = (deadline > reference) ? (deadline - reference) : 0;
    if (delta < kL0Range) {
        level = 0;
        slot = static_cast<uint32_t>(deadline) & kL0Mask;
    } else if (delta < kL1Range) {
        level = 1;
        slot = static_cast<uint32_t>(deadline >> kL0Bits) & kL1Mask;
    } else if (delta < kL2Range) {
        level = 2;
        slot = static_cast<uint32_t>(deadline >> (kL0Bits + kL1Bits)) & kL2Mask;
    } else {
        level = 3;
        slot = 0;
    }
}

// ---------------------------------------------------------------------------
// minimum-deadline cache (kept exact)
// ---------------------------------------------------------------------------

void TimerCore::OnNodeArmed(uint64_t deadline) {
    if (!min_valid_) {
        // Nothing armed before this node? Then this node *is* the minimum.
        if (total_tasks_ == 1) {
            min_deadline_ = deadline;
            min_holders_ = 1;
            min_valid_ = true;
        }
        return;
    }
    if (deadline < min_deadline_) {
        min_deadline_ = deadline;
        min_holders_ = 1;
    } else if (deadline == min_deadline_) {
        ++min_holders_;
    }
}

void TimerCore::OnNodeDisarmed(uint64_t deadline) {
    if (!min_valid_ || deadline != min_deadline_) {
        return;
    }
    if (min_holders_ > 0) {
        --min_holders_;
    }
    if (min_holders_ == 0) {
        min_valid_ = false;
    }
}

// ---------------------------------------------------------------------------
// attach / detach
// ---------------------------------------------------------------------------

// `reference` must always be the wheel's own position (`current_ms_`), never
// wall-clock `now`.
//
// The wheel's invariant is that L0 slot s holds exactly the deadlines in
// [current_ms_, current_ms_ + 256) whose low 8 bits are s. ComputeDest derives
// the level from `deadline - reference`, so passing a `now` that runs ahead of
// `current_ms_` understates that distance and files far-future deadlines into
// L0. With the wheel D ms behind, a deadline `now + delay` is really
// `current_ms_ + D + delay` away; at D + delay == 256 it was placed in L0 slot
// `current_ms_ & 255` -- the slot currently being drained -- so the timer fired
// 256 ms early and, for a self-rescheduling timer, kept refilling the slot
// FireSlot was emptying (observed: 10.7M callbacks in 20 s, 89% of the event
// loop, UDP reads starved into multi-second handshake stalls).
void TimerCore::AttachFromIdle(uint32_t index, uint64_t reference) {
    Entry& entry = slab_[index];
    uint64_t deadline = entry.it->deadline;

    int8_t level = 0;
    uint32_t slot = 0;
    ComputeDest(deadline, reference, level, slot);

    Slot& dst = ListOf(level, slot);
    // std::list::splice keeps the iterator valid; it simply now refers to the
    // element inside `dst`. That is what makes Rearm allocation-free.
    dst.splice(dst.end(), idle_, entry.it);
    entry.level = level;
    entry.slot = slot;
    if (level == 0) {
        SetL0Bit(slot);
    }
    ++total_tasks_;
    OnNodeArmed(deadline);
}

void TimerCore::DetachArmed(uint32_t index, bool destroy) {
    Entry& entry = slab_[index];
    int8_t level = entry.level;
    uint32_t slot = entry.slot;
    uint64_t deadline = entry.it->deadline;

    Slot& src = ListOf(level, slot);
    if (destroy) {
        src.erase(entry.it);
        entry.has_node = false;
    } else {
        idle_.splice(idle_.end(), src, entry.it);
    }
    entry.level = -1;
    entry.slot = 0;
    if (level == 0 && wheel0_[slot].empty()) {
        ClearL0Bit(slot);
    }
    --total_tasks_;
    OnNodeDisarmed(deadline);
}

void TimerCore::DestroyParked(uint32_t index) {
    Entry& entry = slab_[index];
    idle_.erase(entry.it);
    entry.has_node = false;
}

// ---------------------------------------------------------------------------
// public: arm / rearm / cancel
// ---------------------------------------------------------------------------

uint32_t TimerCore::Arm(std::function<void()> cb, std::weak_ptr<void> owner, bool has_owner, uint32_t delay_ms,
    uint32_t interval_ms, uint64_t now, uint32_t& out_gen) {
    if (!initialized_) {
        current_ms_ = now;
        initialized_ = true;
    }

    uint32_t index = AllocEntry();
    if (index == kNoEntry) {
        return kNoEntry;
    }

    uint64_t deadline = now + delay_ms;
    // A callback that arms a timer while we are firing must not land in the slot
    // currently being drained, otherwise the new timer would either be replayed
    // in the same pass or be stranded until the wheel wraps.
    if (firing_depth_ > 0 && deadline <= current_ms_) {
        deadline = current_ms_ + 1;
    } else if (deadline < current_ms_) {
        deadline = current_ms_;
    }

    TimerNode node;
    node.cb = std::move(cb);
    node.owner = std::move(owner);
    node.has_owner = has_owner;
    node.deadline = deadline;
    node.interval = interval_ms;
    node.entry = index;

    idle_.push_back(std::move(node));
    Entry& entry = slab_[index];
    entry.it = std::prev(idle_.end());
    entry.has_node = true;
    entry.level = -1;
    // Placement is relative to where the *wheel* stands, not to wall clock. See
    // the note above AttachFromIdle(): using `now` here breaks the L0 invariant
    // whenever the wheel is behind real time.
    AttachFromIdle(index, current_ms_);

    out_gen = slab_[index].gen;
    return index;
}

void TimerCore::ArmDetached(std::function<void()> cb, uint32_t delay_ms, uint64_t now) {
    if (!initialized_) {
        current_ms_ = now;
        initialized_ = true;
    }

    uint64_t deadline = now + delay_ms;
    if (firing_depth_ > 0 && deadline <= current_ms_) {
        deadline = current_ms_ + 1;
    } else if (deadline < current_ms_) {
        deadline = current_ms_;
    }

    TimerNode node;
    node.cb = std::move(cb);
    node.deadline = deadline;
    node.entry = kNoEntry;

    int8_t level = 0;
    uint32_t slot = 0;
    ComputeDest(deadline, current_ms_, level, slot);
    Slot& dst = ListOf(level, slot);
    dst.push_back(std::move(node));
    if (level == 0) {
        SetL0Bit(slot);
    }
    ++total_tasks_;
    OnNodeArmed(deadline);
}

bool TimerCore::Rearm(uint32_t index, uint32_t gen, uint32_t delay_ms, uint64_t now) {
    if (index >= slab_.size()) {
        return false;
    }
    Entry& entry = slab_[index];
    if (!entry.in_use || entry.gen != gen || !entry.has_node) {
        return false;
    }

    uint64_t deadline = now + delay_ms;
    if (firing_depth_ > 0 && deadline <= current_ms_) {
        deadline = current_ms_ + 1;
    } else if (deadline < current_ms_) {
        deadline = current_ms_;
    }

    bool armed = entry.level >= 0;
    uint64_t old_deadline = entry.it->deadline;

    // Fast path for the dominant QUIC pattern (per-packet PTO / idle reset):
    // the timer being rearmed is the sole holder of the current minimum and the
    // new deadline is not later, so the cache can follow it without a rescan.
    bool keep_min =
        armed && min_valid_ && min_holders_ == 1 && old_deadline == min_deadline_ && deadline <= min_deadline_;

    if (armed) {
        DetachArmed(index, /*destroy=*/false);
    }
    slab_[index].it->deadline = deadline;
    slab_[index].it->interval = 0;
    AttachFromIdle(index, current_ms_);

    if (keep_min) {
        min_deadline_ = deadline;
        min_holders_ = 1;
        min_valid_ = true;
    }
    return true;
}

bool TimerCore::CancelLocal(uint32_t index, uint32_t gen) {
    if (index >= slab_.size()) {
        return false;
    }
    Entry& entry = slab_[index];
    if (!entry.in_use || entry.gen != gen || !entry.has_node) {
        return false;
    }

    if (entry.level >= 0) {
        DetachArmed(index, /*destroy=*/true);
    } else {
        DestroyParked(index);
    }
    return true;
}

void TimerCore::ReleaseEntry(uint32_t index, uint32_t gen) {
    if (index >= slab_.size()) {
        return;
    }
    Entry& entry = slab_[index];
    if (!entry.in_use || entry.gen != gen) {
        return;
    }

    if (entry.has_node) {
        if (entry.level >= 0) {
            DetachArmed(index, /*destroy=*/true);
        } else {
            DestroyParked(index);
        }
    }
    entry.in_use = false;
    ++entry.gen;
    entry.next_free = free_head_;
    free_head_ = index;
}

bool TimerCore::IsActive(uint32_t index, uint32_t gen) const {
    if (index >= slab_.size()) {
        return false;
    }
    const Entry& entry = slab_[index];
    return entry.in_use && entry.gen == gen && entry.has_node && entry.level >= 0;
}

void TimerCore::CancelRemote(uint32_t index, uint32_t gen) {
    {
        std::lock_guard<std::mutex> lock(remote_mu_);
        remote_pending_.emplace_back(index, gen);
    }
    remote_pending_count_.fetch_add(1, std::memory_order_release);
}

void TimerCore::DrainRemoteCancels() {
    std::vector<std::pair<uint32_t, uint32_t>> pending;
    {
        std::lock_guard<std::mutex> lock(remote_mu_);
        if (remote_pending_.empty()) {
            remote_pending_count_.store(0, std::memory_order_relaxed);
            return;
        }
        pending.swap(remote_pending_);
        remote_pending_count_.store(0, std::memory_order_relaxed);
    }

    for (const auto& item : pending) {
        CancelLocal(item.first, item.second);
        ReleaseEntry(item.first, item.second);
    }
}

// ---------------------------------------------------------------------------
// engine
// ---------------------------------------------------------------------------

int32_t TimerCore::MinTime(uint64_t now) {
    if (total_tasks_ == 0) {
        min_valid_ = false;
        return -1;
    }

    if (!min_valid_) {
        uint32_t holders = 0;
        uint64_t earliest = EarliestDeadline(&holders);
        if (earliest == kInvalidDeadline) {
            return -1;
        }
        min_deadline_ = earliest;
        min_holders_ = holders;
        min_valid_ = true;
    }

    if (min_deadline_ <= now) {
        return 0;
    }
    uint64_t diff = min_deadline_ - now;
    if (diff > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
        return std::numeric_limits<int32_t>::max();
    }
    return static_cast<int32_t>(diff);
}

void TimerCore::Run(uint64_t now) {
    if (remote_pending_count_.load(std::memory_order_acquire) != 0) {
        DrainRemoteCancels();
    }

    if (!initialized_) {
        current_ms_ = now;
        initialized_ = true;
        return;
    }
    if (now < current_ms_) {
        return;
    }
    Tick(now);
}

void TimerCore::Clear() {
    {
        std::lock_guard<std::mutex> lock(remote_mu_);
        remote_pending_.clear();
        remote_pending_count_.store(0, std::memory_order_relaxed);
    }

    for (Slot& slot : wheel0_) {
        slot.clear();
    }
    for (Slot& slot : wheel1_) {
        slot.clear();
    }
    for (Slot& slot : wheel2_) {
        slot.clear();
    }
    overflow_.clear();
    idle_.clear();
    wheel0_occ_ = {0, 0, 0, 0};

    free_head_ = kNoEntry;
    for (size_t i = 0; i < slab_.size(); ++i) {
        Entry& entry = slab_[i];
        ++entry.gen;
        entry.in_use = false;
        entry.has_node = false;
        entry.level = -1;
        entry.slot = 0;
        entry.next_free = free_head_;
        free_head_ = static_cast<uint32_t>(i);
    }

    total_tasks_ = 0;
    min_valid_ = false;
    min_holders_ = 0;
}

void TimerCore::Cascade(int level, uint32_t slot_idx) {
    Slot* src = nullptr;
    switch (level) {
        case 1:
            src = &wheel1_[slot_idx];
            break;
        case 2:
            src = &wheel2_[slot_idx];
            break;
        default:
            src = &overflow_;
            break;
    }
    if (src->empty()) {
        return;
    }

    // Detach the whole slot BEFORE re-attaching anything.
    //
    // Re-attachment may legitimately target the very list we are draining: a
    // timer more than one full L2 revolution away is still an overflow timer
    // after the overflow list is cascaded, so it goes straight back into
    // overflow_. Draining `*src` in place would then see it again and spin
    // forever (reproduced by a 2-hour timer crossing an L2 epoch boundary).
    //
    // While nodes sit in `pending`, their slab entries still name the now-empty
    // source slot. That is safe because Cascade invokes no callbacks and drains
    // no remote cancels, so nothing can observe the intermediate state.
    Slot pending;
    pending.swap(*src);

    // Splicing moves nodes without the per-node free + malloc + 64-byte copy
    // that the previous implementation paid (measured 7.46 us for a single tick
    // with 20k timers).
    while (!pending.empty()) {
        auto it = pending.begin();
        uint32_t index = it->entry;
        uint64_t deadline = it->deadline;

        idle_.splice(idle_.end(), pending, it);
        --total_tasks_;
        OnNodeDisarmed(deadline);

        if (index == kNoEntry) {
            // Detached node: no slab entry, re-place it directly.
            int8_t new_level = 0;
            uint32_t new_slot = 0;
            ComputeDest(deadline, current_ms_, new_level, new_slot);
            Slot& dst = ListOf(new_level, new_slot);
            dst.splice(dst.end(), idle_, it);
            if (new_level == 0) {
                SetL0Bit(new_slot);
            }
            ++total_tasks_;
            OnNodeArmed(deadline);
        } else {
            Entry& entry = slab_[index];
            entry.level = -1;
            entry.slot = 0;
            entry.it = it;
            AttachFromIdle(index, current_ms_);
        }
    }
}

uint32_t TimerCore::NextL0From(uint32_t from) const {
    if (from >= kL0Size) {
        return kL0Size;
    }
    uint32_t word = from >> 6;
    uint32_t bit = from & 63;
    uint64_t masked = wheel0_occ_[word] & (~0ull << bit);
    if (masked != 0) {
        return (word << 6) + static_cast<uint32_t>(__builtin_ctzll(masked));
    }
    for (uint32_t w = word + 1; w < 4; ++w) {
        if (wheel0_occ_[w] != 0) {
            return (w << 6) + static_cast<uint32_t>(__builtin_ctzll(wheel0_occ_[w]));
        }
    }
    return kL0Size;
}

void TimerCore::FireSlot(uint32_t c0) {
    Slot& slot = wheel0_[c0];
    ++firing_depth_;

    // Drain the slot in place. Nodes still waiting in this slot keep their
    // level/slot metadata valid, so a callback that cancels one of them (or a
    // remote cancel drained mid-pass) resolves to the correct list.
    //
    // Fire only what was already queued when the pass began. Nothing armed from
    // inside a callback can legitimately land here: every placement is computed
    // against `current_ms_`, so reaching L0 slot c0 requires a distance that is
    // a non-zero multiple of 256 -- and 256 is already an L1 deadline. The
    // budget therefore never ends the loop in correct operation; it exists so
    // that a future placement bug degrades into a late timer instead of an
    // unbreakable spin, which is how the `reference`-vs-`current_ms_` bug used
    // to manifest.
    size_t budget = slot.size();
    while (budget > 0 && !slot.empty()) {
        --budget;
        auto it = slot.begin();
        uint32_t index = it->entry;

        if (remote_pending_count_.load(std::memory_order_acquire) != 0) {
            DrainRemoteCancels();
            // The drain may have erased this very node.
            if (slot.empty()) {
                break;
            }
            it = slot.begin();
            index = it->entry;
        }

        if (index == kNoEntry) {
            // Detached, fire-and-forget: fire then destroy.
            bool skip = it->has_owner && it->owner.expired();
            uint64_t deadline = it->deadline;
            std::function<void()> cb = std::move(it->cb);
            slot.erase(it);
            --total_tasks_;
            OnNodeDisarmed(deadline);
            if (!skip && cb) {
                cb();
            }
            continue;
        }

        // Park the node so its handle stays valid across the fire.
        DetachArmed(index, /*destroy=*/false);

        // Lock the owner and keep it alive for the whole duration of the callback.
        // This is the documented contract of the owner guard (if_timer_scheduler.h):
        // it is what makes it safe for a callback to reference its owning object.
        // Concretely, even if another thread drops the last external reference to
        // the owner (e.g. the connection) while this callback is in flight, the
        // owner cannot be destroyed — and its vptr/state torn — until the callback
        // returns. Without this, TSan reports a dtor-vs-callback data race.
        std::shared_ptr<void> owner_guard;
        bool skip = false;
        if (slab_[index].it->has_owner) {
            owner_guard = slab_[index].it->owner.lock();
            if (!owner_guard) {
                skip = true;
            }
        }
        uint32_t interval = slab_[index].it->interval;
        // Snapshot the release token: if the callback releases this entry and a
        // nested Arm() recycles the slot, the post-fire bookkeeping below must
        // not touch the new occupant.
        uint32_t gen_at_fire = slab_[index].gen;

        // Move the callback out of the node before invoking it. The callback is
        // allowed to cancel its own timer, which destroys the node; destroying a
        // std::function while its target is executing is undefined behaviour.
        // Moving it out also releases the callback's captures as soon as a
        // one-shot has fired, instead of holding them until the handle dies.
        std::function<void()> cb;
        if (!skip) {
            cb = std::move(slab_[index].it->cb);
        }
        if (cb) {
            cb();
        }

        // The callback (or a remote cancel drained by it) may have cancelled or
        // released this entry, so re-validate against the slab before re-arming.
        // Note slab_ may have been reallocated by a nested Arm(), so it must be
        // re-indexed rather than accessed through a previously bound reference.
        bool still_ours = index < slab_.size() && slab_[index].in_use && slab_[index].gen == gen_at_fire;
        if (!skip && still_ours && slab_[index].has_node) {
            if (!slab_[index].it->cb) {
                slab_[index].it->cb = std::move(cb);
            }
            if (interval != 0 && slab_[index].level < 0) {
                slab_[index].it->deadline = current_ms_ + interval;
                AttachFromIdle(index, current_ms_);
            }
        }
    }

    if (slot.empty()) {
        ClearL0Bit(c0);
    }
    --firing_depth_;
}

void TimerCore::Tick(uint64_t now) {
    while (current_ms_ <= now) {
        uint32_t c0 = static_cast<uint32_t>(current_ms_) & kL0Mask;
        uint32_t c1 = static_cast<uint32_t>(current_ms_ >> kL0Bits) & kL1Mask;
        uint32_t c2 = static_cast<uint32_t>(current_ms_ >> (kL0Bits + kL1Bits)) & kL2Mask;

        if (c0 == 0) {
            if (c1 == 0) {
                if (c2 == 0) {
                    Cascade(3, 0);
                }
                Cascade(2, c2);
            }
            Cascade(1, c1);
        }

        if (wheel0_[c0].empty()) {
            // Skip the empty milliseconds in one step instead of iterating each
            // one. We never jump past an L0 epoch boundary, so cascades still
            // happen on time.
            uint32_t next = NextL0From(c0 + 1);
            uint64_t base = current_ms_ - c0;
            uint64_t target = (next >= kL0Size) ? (base + kL0Range) : (base + next);
            if (target > now) {
                current_ms_ = now + 1;
                return;
            }
            current_ms_ = target;
            continue;
        }

        FireSlot(c0);
        ++current_ms_;
    }
}

uint64_t TimerCore::EarliestDeadline(uint32_t* holders) const {
    uint64_t earliest = kInvalidDeadline;
    uint32_t count = 0;

    auto scan = [&earliest, &count](const Slot& slot) {
        for (const TimerNode& node : slot) {
            if (node.deadline < earliest) {
                earliest = node.deadline;
                count = 1;
            } else if (node.deadline == earliest) {
                ++count;
            }
        }
    };

    for (const Slot& slot : wheel0_) {
        scan(slot);
    }
    for (const Slot& slot : wheel1_) {
        scan(slot);
    }
    for (const Slot& slot : wheel2_) {
        scan(slot);
    }
    scan(overflow_);

    if (holders != nullptr) {
        *holders = count;
    }
    return earliest;
}

}  // namespace common
}  // namespace quicx
