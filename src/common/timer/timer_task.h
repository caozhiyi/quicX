#ifndef COMMON_TIMER_TIMER_TASK
#define COMMON_TIMER_TIMER_TASK

#include <cstdint>
#include <functional>

namespace quicx {
namespace common {

// Forward-declare so we can friend it.
class TreeMapTimer;

/**
 * @brief A timer task that holds a callback and scheduling metadata.
 *
 * When registered with TreeMapTimer, the internal fields (time_, id_) are
 * populated so the timer can be cancelled by (time_, id_) lookup.
 */
class TimerTask {
public:
    std::function<void()> tcb_;

    TimerTask() {}
    TimerTask(std::function<void()> tcb):
        tcb_(tcb) {}
    TimerTask(const TimerTask& t):
        tcb_(t.tcb_),
        time_(t.time_),
        id_(t.id_) {}
    TimerTask& operator=(const TimerTask&) = default;

    void SetTimeoutCallback(std::function<void()> tcb) { tcb_ = tcb; }
    uint64_t GetId() const { return id_; }
    void SetIdForTest(uint64_t id) { id_ = id; }  // For unit tests only

private:
    uint64_t time_ = 0;
    uint64_t id_ = 0;

    friend class TreeMapTimer;
};

}  // namespace common
}  // namespace quicx

#endif  // COMMON_TIMER_TIMER_TASK