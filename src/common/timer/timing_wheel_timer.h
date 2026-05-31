#ifndef COMMON_TIMER_TIMING_WHEEL_TIMER
#define COMMON_TIMER_TIMING_WHEEL_TIMER

#include <array>
#include <cstdint>
#include <limits>
#include <list>
#include <vector>
#include <unordered_map>

#include "common/timer/if_timer.h"
#include "common/util/random.h"

// __builtin_ctzll is a GCC/Clang intrinsic. Both compilers we support (gcc,
// clang) provide it. MSVC is not a target for this codebase.

namespace quicx {
namespace common {

/**
 * @brief Three-level hierarchical timing wheel.
 *
 * All time values are in milliseconds.
 *
 * Level layout (powers of 2 for cheap modulo / shift arithmetic):
 *
 *   Level 0  –  256 slots × 1 ms/slot  =     256 ms range
 *   Level 1  –   64 slots × 256 ms/slot =  16 384 ms (~16 s) range
 *   Level 2  –   64 slots × 16 384 ms/slot = 1 048 576 ms (~17.5 min) range
 *
 * Timers beyond Level-2 range are stored in an overflow list and re-inserted
 * when the wheel catches up.
 *
 * Ownership model:
 *   Each slot stores TimerTask VALUES (copies), NOT pointers.
 *   AddTimer() copies the caller's task into the appropriate slot and writes
 *   the slot iterator back into the caller's task so that RemoveTimer() can
 *   erase the copy in O(1).
 *
 * Complexity:
 *   AddTimer    – O(1)
 *   RemoveTimer – O(1)
 *   TimerRun    – O(k) where k = number of expired timers in the ticked range
 *   MinTime     – O(1) amortised (lazy scan only after remove/fire)
 *                  Cache is maintained incrementally: AddTimer always updates
 *                  in O(1); after a Remove or Fire that may have invalidated
 *                  the cached minimum, a single O(W) rescan is triggered
 *                  and the result re-cached.
 */
class TimingWheelTimer : public ITimer {
public:
    // ---- wheel geometry ----
    static constexpr uint32_t kL0Bits  = 8;               // 256 slots
    static constexpr uint32_t kL1Bits  = 6;               //  64 slots
    static constexpr uint32_t kL2Bits  = 6;               //  64 slots

    static constexpr uint32_t kL0Size  = 1u << kL0Bits;
    static constexpr uint32_t kL1Size  = 1u << kL1Bits;
    static constexpr uint32_t kL2Size  = 1u << kL2Bits;

    static constexpr uint32_t kL0Mask  = kL0Size - 1;
    static constexpr uint32_t kL1Mask  = kL1Size - 1;
    static constexpr uint32_t kL2Mask  = kL2Size - 1;

    // ms covered by one slot at each level
    static constexpr uint64_t kL0Range = kL0Size;                   //       256
    static constexpr uint64_t kL1Range = kL0Range * kL1Size;        //    16 384
    static constexpr uint64_t kL2Range = kL1Range * kL2Size;        // 1 048 576

    // Each slot holds TimerTask *values* so the wheel owns them.
    using Slot = std::list<TimerTask>;

    TimingWheelTimer();
    ~TimingWheelTimer() = default;

    // ITimer interface
    uint64_t AddTimer(TimerTask& task, uint32_t time_ms, uint64_t now = 0) override;
    bool     RemoveTimer(TimerTask& task) override;
    int32_t  MinTime(uint64_t now = 0) override;
    void     TimerRun(uint64_t now = 0) override;
    bool     Empty() override;

private:
    // Copy `task` into the appropriate wheel slot and update task's placement fields.
    void Insert(TimerTask& task, uint64_t reference);

    // Re-insert all tasks from a higher-level slot into lower levels.
    void Cascade(int level, uint32_t slot);

    // Advance current_ms_ up to (and including) `now`, firing expired slots.
    void Tick(uint64_t now);

    // Compute the earliest absolute deadline using the per-level occupancy
    // bitmaps and per-slot min caches. O(levels) — no full slot scan.
    uint64_t EarliestDeadline() const;

    // ---- bitmap helpers ----
    // Find the lowest set bit in a 256-bit bitmap, starting from `from`
    // (inclusive). Returns 256 if no bit is set in [from, 256).
    static uint32_t Wheel0NextSetFrom(const std::array<uint64_t, 4>& bm, uint32_t from);
    // Find the lowest set bit in a 64-bit bitmap starting from `from`.
    // Returns 64 if no bit is set in [from, 64).
    static uint32_t Wheel64NextSetFrom(uint64_t bm, uint32_t from);

    // Slot occupancy bit set / clear helpers (also keep total bitcount fresh).
    void SetWheel0Bit(uint32_t s)  { wheel0_occ_[s >> 6] |=  (1ull << (s & 63)); }
    void ClrWheel0Bit(uint32_t s)  { wheel0_occ_[s >> 6] &= ~(1ull << (s & 63)); }
    void SetWheel1Bit(uint32_t s)  { wheel1_occ_ |=  (1ull << s); }
    void ClrWheel1Bit(uint32_t s)  { wheel1_occ_ &= ~(1ull << s); }
    void SetWheel2Bit(uint32_t s)  { wheel2_occ_ |=  (1ull << s); }
    void ClrWheel2Bit(uint32_t s)  { wheel2_occ_ &= ~(1ull << s); }

    // Recompute the min deadline for a single L1/L2/overflow slot by
    // scanning the slot's list once. L0 slots all share one deadline so no
    // per-slot min is needed.
    static uint64_t ScanSlotMin(const Slot& slot);

    // ---- wheel storage ----
    std::array<Slot, kL0Size> wheel0_;
    std::array<Slot, kL1Size> wheel1_;
    std::array<Slot, kL2Size> wheel2_;
    Slot                      overflow_;

    // Per-level occupancy bitmaps. Bit s == 1 iff wheelX_[s] is non-empty.
    // These let EarliestDeadline() skip over empty slots in O(words) using
    // ctz, replacing the previous O(W) full scan that consumed ~20% CPU
    // under load.
    std::array<uint64_t, 4> wheel0_occ_ = {0, 0, 0, 0};   // 4 * 64 = 256 bits
    uint64_t                wheel1_occ_ = 0;              // 64 bits
    uint64_t                wheel2_occ_ = 0;              // 64 bits
    bool                    overflow_nonempty_ = false;

    // Per-slot minimum-deadline cache for L1/L2/overflow. Tasks within one
    // L1/L2 slot can have different deadlines (sub-slot ms), so we cache
    // the minimum to avoid scanning the slot's list when computing
    // EarliestDeadline(). L0 slots all share a single ms so no cache.
    // UINT64_MAX means "no task in this slot".
    std::array<uint64_t, kL1Size> wheel1_slot_min_;
    std::array<uint64_t, kL2Size> wheel2_slot_min_;
    uint64_t                      overflow_slot_min_ = std::numeric_limits<uint64_t>::max();

    uint64_t current_ms_  = 0;
    bool     initialized_ = false;
    uint32_t total_tasks_ = 0;

    // ---- minimum-deadline cache ----
    // min_deadline_cache_: the smallest task.time_ currently in the wheel.
    //   UINT64_MAX means "dirty" — recompute on next MinTime() call.
    // Updated in O(1) on AddTimer; invalidated on RemoveTimer / slot fire.
    uint64_t min_deadline_cache_ = std::numeric_limits<uint64_t>::max();
    bool     cache_dirty_        = false;

    RangeRandom random_;

    // Map to keep track of timer locations because user objects are copied.
    std::unordered_map<uint64_t, std::list<TimerTask>::iterator> location_map_;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_TIMER_TIMING_WHEEL_TIMER
