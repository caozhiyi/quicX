#ifndef COMMON_TIMER_TIMER_CONTAINER
#define COMMON_TIMER_TIMER_CONTAINER

#include "timer_1ms.h"

namespace quicx {

class TimerContainer : public Timer {
public:
    TimerContainer(std::shared_ptr<Timer> t, TIMER_CAPACITY accuracy, TIMER_CAPACITY capacity);
    ~TimerContainer();

    bool AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time, bool always = false);
    bool RmTimer(std::weak_ptr<TimerSolt> t);

    // get min next timer out time
    // return >= 0 : the next time
    // < 0 : has no timer
    int32_t MinTime();

    int32_t CurrentTimer();

    uint32_t TimerRun(uint32_t step);
    
    void AddTimerByIndex(std::weak_ptr<TimerSolt> t, uint8_t index);
    
private:
    int32_t LocalMinTime();
    
private:
    std::vector<std::list<std::weak_ptr<TimerSolt>>> _timer_wheel;
    std::shared_ptr<Timer> _sub_timer;
    uint32_t _cur_index;
    Bitmap _bitmap;

    TIMER_CAPACITY _accuracy;
    TIMER_CAPACITY _capacity;
    uint32_t _max_size;
};

}

#endif