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

    // Scan all slots for the minimum absolute deadline.
    uint64_t EarliestDeadline() const;

    // ---- wheel storage ----
    std::array<Slot, kL0Size> wheel0_;
    std::array<Slot, kL1Size> wheel1_;
    std::array<Slot, kL2Size> wheel2_;
    Slot                      overflow_;

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
