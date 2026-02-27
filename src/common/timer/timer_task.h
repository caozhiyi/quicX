#ifndef COMMON_TIMER_TIMER_TASK
#define COMMON_TIMER_TIMER_TASK

#include <cstdint>
#include <functional>
#include <list>

namespace quicx {
namespace common {

// Forward-declare so we can friend it.
class TreeMapTimer;
class TimingWheelTimer;

/**
 * @brief A timer task that holds a callback and placement metadata.
 *
 * When registered with TimingWheelTimer, the internal fields (time_, id_,
 * wheel_idx_, slot_idx_, list_it_) are populated so the timer can be
 * cancelled in O(1) time.
 */
class TimerTask {
public:
    std::function<void()> tcb_;

    TimerTask() {}
    TimerTask(std::function<void()> tcb): tcb_(tcb) {}
    TimerTask(const TimerTask& t)
        : tcb_(t.tcb_), time_(t.time_), id_(t.id_),
          wheel_idx_(t.wheel_idx_), slot_idx_(t.slot_idx_), list_it_(t.list_it_) {}

    void SetTimeoutCallback(std::function<void()> tcb) { tcb_ = tcb; }
    uint64_t GetId() const { return id_; }
    void SetIdForTest(uint64_t id) { id_ = id; }  // For unit tests only

private:
    uint64_t time_ = 0;
    uint64_t id_   = 0;

    // Timing-wheel placement metadata.
    // wheel_idx_ == -1 means "not currently registered in the wheel".
    // Values 0/1/2 → wheel0_/wheel1_/wheel2_; 3 → overflow list.
    int8_t   wheel_idx_ = -1;
    uint32_t slot_idx_  = 0;
    // Iterator into the slot's value-list (list<TimerTask>).
    // Valid only when wheel_idx_ >= 0.
    std::list<TimerTask>::iterator list_it_;

    friend class TreeMapTimer;
    friend class TimingWheelTimer;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_TIMER_TIMER_TASK