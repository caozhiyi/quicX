// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_TIMER_TIMER_TASK
#define COMMON_TIMER_TIMER_TASK

#include <memory>
#include <cstdint>
#include <functional>

namespace quicx {
namespace common {

typedef std::function<void()> TimerCallback;

class TimerTask {
public:
    TimerCallback tcb_;
    TimerTask() {}
    TimerTask(TimerCallback tcb): tcb_(tcb) {}
    TimerTask(const TimerTask& t): tcb_(t.tcb_), time_(t.time_), id_(t.id_) {}
private:
    uint64_t time_;
    uint64_t id_;
friend class TreeMapTimer;
};

}
}

#endif