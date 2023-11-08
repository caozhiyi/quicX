// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#include <limits>
#include "common/util/time.h"
#include "common/timer/treemap_timer.h"

namespace quicx {


TreeMapTimer::TreeMapTimer(): _random(0, (int32_t)std::numeric_limits<int32_t>::max()) {
    
}

TreeMapTimer::~TreeMapTimer() {

}

uint64_t TreeMapTimer::AddTimer(TimerTask& task, uint32_t time, uint64_t now) {
    if (now == 0) {
        now = UTCTimeMsec();
    }

    task._time = now + time;
    task._id = _random.Random();
    _timer_map[task._time][task._id] = task;
    return task._id;
}

bool TreeMapTimer::RmTimer(TimerTask& task) {
    auto iter = _timer_map.find(task._time);
    if (iter != _timer_map.end()) {
        iter->second.erase(task._id);
        if (iter->second.empty()) {
            _timer_map.erase(iter);
        }
        return true;
    }
    return false;
}

int32_t TreeMapTimer::MinTime(uint64_t now) {
    if (_timer_map.empty()) {
        return -1;
    }
    
    if (now == 0) {
        now = UTCTimeMsec();
    }
    return _timer_map.begin()->first - now;
}

void TreeMapTimer::TimerRun(uint64_t now) {
    if (now == 0) {
        now = UTCTimeMsec();
    }
    for (auto iter = _timer_map.begin(); iter != _timer_map.end();) {
        if (iter->first <= now) {
            for (auto task = iter->second.begin(); task != iter->second.end(); task++) {
                if (task->second._tcb) {
                    task->second._tcb();
                }
            }
            iter = _timer_map.erase(iter);
        } else {
            break;
        }
    }
}

bool TreeMapTimer::Empty() {
    return _timer_map.empty();
}

}
