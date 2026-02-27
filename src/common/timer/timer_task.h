#ifndef COMMON_TIMER_TIMER_TASK
#define COMMON_TIMER_TIMER_TASK

#include <cstdint>
#include <functional>

namespace quicx {
namespace common {

class TimerTask {
public:
    std::function<void()> tcb_;
    TimerTask() {}
    TimerTask(std::function<void()> tcb): tcb_(tcb) {}
    TimerTask(const TimerTask& t): tcb_(t.tcb_), time_(t.time_), id_(t.id_) {}
    void SetTimeoutCallback(std::function<void()> tcb) { tcb_ = tcb; }
    uint64_t GetId() const { return id_; }
    void SetIdForTest(uint64_t id) { id_ = id; }  // For unit tests only
private:
    uint64_t time_;
    uint64_t id_;
friend class TreeMapTimer;
};

}
}

#endif