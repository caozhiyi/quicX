#ifndef COMMON_TIMER_INTERFACE
#define COMMON_TIMER_INTERFACE

#include <memory>
#include "timer_interface.h"

namespace quicx {

enum TimeDefine {
    MILLISECOND = 1,
    SECOND      = 1000,
    MINUTE      = 60 * 1000,
};

enum TimerCode {
    NO_TIMER = -1 // don't have timer
};

class Timer {
public:
    Timer() {}
    ~Timer() {}

    virtual bool AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time, bool always = false) = 0;
    virtual bool RmTimer(std::weak_ptr<TimerSolt> t) = 0;

    // get min next timer out time
    // return >= 0 : the next time
    // < 0 : has no timer
    virtual int32_t MinTime() = 0;

    virtual void TimerRun(uint32_t step) = 0;
};

}

#endif