#ifndef COMMON_TIMER_TIMER_1MS
#define COMMON_TIMER_TIMER_1MS

#include <set>
#include <vector>

#include "util/bitmap.h"
#include "timer_solt.h"
#include "timer_interface.h"

namespace quicx {

// accuracy is 1 millisecond
// capacity is 50
// maximum time 50 millisecond
class Timer1ms : public Timer {
public:
    Timer1ms();
    ~Timer1ms();

    bool AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time, bool always = false);
    bool RmTimer(std::weak_ptr<TimerSolt> t);

    // get min next timer out time
    // return >= 0 : the next time
    // < 0 : has no timer
    int32_t MinTime();

    void TimerRun(uint32_t time);

    void AddTimer(std::weak_ptr<TimerSolt> t, uint8_t index);
    
private:
    std::vector<std::set<std::weak_ptr<TimerSolt>>> _timer_wheel;
    uint32_t _cur_index;
    Bitmap _bitmap;
};

}

#endif