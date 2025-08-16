// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <limits>
#include "common/log/log.h"
#include "common/util/time.h"
#include "common/timer/treemap_timer.h"

namespace quicx {
namespace common {


TreeMapTimer::TreeMapTimer(): random_(0, (int32_t)std::numeric_limits<int32_t>::max()) {
    
}

TreeMapTimer::~TreeMapTimer() {

}

uint64_t TreeMapTimer::AddTimer(TimerTask& task, uint32_t time_ms, uint64_t now) {
    if (now == 0) {
        now = UTCTimeMsec();
    }

    task.time_ = now + time_ms;
    task.id_ = random_.Random();
    timer_map_[task.time_][task.id_] = task;
    
    common::LOG_DEBUG("TreeMapTimer: Added timer with ID %lu, time %lu (now=%lu, timeout=%u)", 
                     task.id_, task.time_, now, time_ms);
    
    return task.id_;
}

bool TreeMapTimer::RmTimer(TimerTask& task) {
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
        common::LOG_DEBUG("TreeMapTimer: No timers in map");
        return -1;
    }
    
    if (now == 0) {
        now = UTCTimeMsec();
    }
    
    int32_t next_time = (int32_t)(timer_map_.begin()->first - now);
    common::LOG_DEBUG("TreeMapTimer: Next timer in %d ms (map size: %zu)", next_time, timer_map_.size());
    return next_time;
}

void TreeMapTimer::TimerRun(uint64_t now) {
    if (now == 0) {
        now = UTCTimeMsec();
    }
    
    int executed_count = 0;
    for (auto iter = timer_map_.begin(); iter != timer_map_.end();) {
        if (iter->first <= now) {
            for (auto task = iter->second.begin(); task != iter->second.end(); task++) {
                if (task->second.tcb_) {
                    task->second.tcb_();
                    executed_count++;
                }
            }
            iter = timer_map_.erase(iter);
        } else {
            break;
        }
    }
    
    if (executed_count > 0) {
        common::LOG_DEBUG("TimerRun executed %d timers at time %lu", executed_count, now);
    }
}

bool TreeMapTimer::Empty() {
    return timer_map_.empty();
}

}
}