// Use of this source code is governed by a BSD 3-Clause License
// that can be found in the LICENSE file.

// Author: caozhiyi (caozhiyi5@gmail.com)

#ifndef COMMON_TIMER_TREEMAP_TIMER
#define COMMON_TIMER_TREEMAP_TIMER

#include <map>
#include <unordered_map>
#include "common/util/random.h"
#include "common/timer/timer_interface.h"

namespace quicx {
namespace common {

// timer interface, timer inherits from this.
class TreeMapTimer:
    public ITimer {
public:
    TreeMapTimer();
    ~TreeMapTimer();

    virtual uint64_t AddTimer(TimerTask& task, uint32_t time, uint64_t now = 0);
    virtual bool RmTimer(TimerTask& task);

    // get min next time out time
    // return: 
    // >= 0  : the next time
    //  < 0  : has no timer
    virtual int32_t MinTime(uint64_t now = 0);

    // timer wheel run time 
    // return carry
    virtual void TimerRun(uint64_t now = 0);

    virtual bool Empty();
private:
    RangeRandom _random;
    // time => id => task
    std::map<uint64_t, std::unordered_map<uint64_t, TimerTask>> _timer_map;
};

}
}

#endif