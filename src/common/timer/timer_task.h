// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_TIMER_TIMER_TASK
#define COMMON_TIMER_TIMER_TASK

#include <memory>
#include <cstdint>
#include <functional>

namespace quicx {

typedef std::function<void()> TimerCallback;

class TimerTask {
public:
    TimerCallback _tcb;
    TimerTask() {}
    TimerTask(TimerCallback tcb): _tcb(tcb) {}
    TimerTask(const TimerTask& t): _tcb(t._tcb), _time(t._time), _id(t._id) {}
private:
    uint64_t _time;
    uint64_t _id;
friend class TreeMapTimer;
};

}

#endif