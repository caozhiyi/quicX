#ifndef COMMON_TIMER_TREEMAP_TIMER
#define COMMON_TIMER_TREEMAP_TIMER

#include <map>
#include <unordered_map>
#include "common/timer/if_timer.h"

namespace quicx {
namespace common {

// timer interface, timer inherits from this.
class TreeMapTimer:
    public ITimer {
public:
    TreeMapTimer();
    ~TreeMapTimer();

    virtual uint64_t AddTimer(TimerTask& task, uint32_t time_ms, uint64_t now = 0);
    virtual bool RemoveTimer(TimerTask& task);

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
    uint64_t next_id_ = 0;  // monotonically increasing timer ID
    // time => id => task
    std::map<uint64_t, std::unordered_map<uint64_t, TimerTask>> timer_map_;
};

}
}

#endif