// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <limits>
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
        return -1;
    }
    
    if (now == 0) {
        now = UTCTimeMsec();
    }
    return (int32_t)(timer_map_.begin()->first - now);
}

void TreeMapTimer::TimerRun(uint64_t now) {
    if (now == 0) {
        now = UTCTimeMsec();
    }
    for (auto iter = timer_map_.begin(); iter != timer_map_.end();) {
        if (iter->first <= now) {
            for (auto task = iter->second.begin(); task != iter->second.end(); task++) {
                if (task->second.tcb_) {
                    task->second.tcb_();
                }
            }
            iter = timer_map_.erase(iter);
        } else {
            break;
        }
    }
}

bool TreeMapTimer::Empty() {
    return timer_map_.empty();
}

}
}