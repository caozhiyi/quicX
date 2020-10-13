#ifndef COMMON_TIMER_WHEEL
#define COMMON_TIMER_WHEEL

#include <set>
#include <vector>

#include "bitmap.h"
#include "timer_interface.h"


class TimerWheel : public Timer {
public:
    TimerWheel(uint32_t accuracy, uint32_t size);
    ~TimerWheel();

    bool AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time);
    bool RmTimer(std::weak_ptr<TimerSolt> t);
    bool RmTimer(std::weak_ptr<TimerSolt> t, uint32_t index);

    // get min next timer out time
    // return >= 0 : the next time
    // < 0 : has no timer
    int32_t MinTime();

    void TimerRun(uint32_t step);
    
private:
    uint32_t _accuracy;
    uint32_t _size;
    std::vector<std::set<std::weak_ptr<TimerSolt>>> _timer_wheel;

    uint32_t _cur_index;
    Bitmap _bitmap;
};

#endif