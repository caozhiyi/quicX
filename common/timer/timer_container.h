#ifndef COMMON_TIMER_50MS
#define COMMON_TIMER_50MS

#include "timer_1ms.h"

namespace quicx {

class TimerContainer {
public:
    TimerContainer(std::shared_ptr<Timer> t, uint32_t accuracy, uint32_t capacity);
    ~TimerContainer();

    bool AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time, bool always = false);
    bool RmTimer(std::weak_ptr<TimerSolt> t);

    // get min next timer out time
    // return >= 0 : the next time
    // < 0 : has no timer
    int32_t MinTime();

    void TimerRun(uint32_t step);
    
private:
    std::vector<std::set<std::weak_ptr<TimerSolt>>> _timer_wheel;
    std::shared_ptr<Timer> _sub_timer;
    uint32_t _cur_index;
    Bitmap _bitmap;

    uint32_t _accuracy;
    uint32_t _capacity;
};

}

#endif