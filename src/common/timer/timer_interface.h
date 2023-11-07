// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_TIMER_TIMER_INTERFACE
#define COMMON_TIMER_TIMER_INTERFACE

#include "common/timer/timer_task.h"

namespace quicx {

// timer interface, timer inherits from this.
class ITimer {
public:
    ITimer() {}
    ~ITimer() {}

    virtual uint64_t AddTimer(TimerTask& task, uint32_t time, uint64_t now = 0) = 0;
    virtual void RmTimer(TimerTask& task) = 0;

    // get min next time out time
    // return: 
    // >= 0  : the next time
    //  < 0  : has no timer
    virtual int32_t MinTime(uint64_t now = 0) = 0;

    // timer wheel run time 
    virtual void TimerRun(uint64_t now = 0) = 0;

    virtual bool Empty() = 0;
};

}

#endif