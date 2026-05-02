#include "common/timer/treemap_timer.h"
#include "common/util/time.h"

namespace quicx {
namespace common {

TreeMapTimer::TreeMapTimer():
    next_id_(0) {}

TreeMapTimer::~TreeMapTimer() {}

uint64_t TreeMapTimer::AddTimer(TimerTask& task, uint32_t time_ms, uint64_t now) {
    if (now == 0) {
        now = UTCTimeMsec();
    }

    task.time_ = now + time_ms;
    task.id_ = ++next_id_;
    timer_map_[task.time_][task.id_] = task;

    return task.id_;
}

bool TreeMapTimer::RemoveTimer(TimerTask& task) {
    auto iter = timer_map_.find(task.time_);
    if (iter != timer_map_.end()) {
        iter->second.erase(task.id_);
        if (iter->second.empty()) {
            timer_map_.erase(iter);
        }
        return true;
    }
    return false;
}

int32_t TreeMapTimer::MinTime(uint64_t now) {
    if (timer_map_.empty()) {
        return -1;
    }

    if (now == 0) {
        now = UTCTimeMsec();
    }

    int32_t next_time = (int32_t)(timer_map_.begin()->first - now);

    // If next_time is negative, it means the timer has already expired
    // Return 0 to indicate immediate execution
    if (next_time < 0) {
        return 0;
    }

    return next_time;
}

void TreeMapTimer::TimerRun(uint64_t now) {
    if (now == 0) {
        now = UTCTimeMsec();
    }

    int executed_count = 0;
    std::vector<std::function<void()>> callbacks;

    // Collect all expired timers
    for (auto iter = timer_map_.begin(); iter != timer_map_.end();) {
        if (iter->first <= now) {
            // Store the tasks to execute to avoid iterator invalidation
            for (auto task = iter->second.begin(); task != iter->second.end(); task++) {
                if (task->second.tcb_) {
                    callbacks.push_back(task->second.tcb_);
                }
            }

            // Remove the time slot from the map
            iter = timer_map_.erase(iter);
        } else {
            break;
        }
    }

    // Execute callbacks after removing from map to avoid iterator invalidation
    for (auto& callback : callbacks) {
        callback();
        executed_count++;
    }
}

bool TreeMapTimer::Empty() {
    return timer_map_.empty();
}

}  // namespace common
}  // namespace quicx