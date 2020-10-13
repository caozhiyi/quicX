#include <cassert>
#include "timer_wheel.h"


TimerWheel::TimerWheel(uint32_t accuracy, uint32_t size) : 
   _accuracy(accuracy), _size(size) {
    assert(accuracy < size);
    assert(size > 0);
    assert(accuracy > 0);
    assert(_bitmap.Init(size));

    _cur_index = 0;
    _timer_wheel.resize(size);
}

TimerWheel::~TimerWheel() {

}

bool TimerWheel::AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time) {
    if (time > _size * _accuracy) {
        return false;
    }

    uint32_t index = time / _accuracy;
    _timer_wheel[index].insert(t);
    return _bitmap.Insert(index);
}

bool TimerWheel::RmTimer(std::weak_ptr<TimerSolt> t) {
    return true;
}

bool TimerWheel::RmTimer(std::weak_ptr<TimerSolt> t, uint32_t index) {
    if (index > _size) {
        return false;
    }

    auto bucket = _timer_wheel[_cur_index];
    if (bucket.size() > 0) {
        bucket.erase(t);
    }
    return true;
}

int32_t TimerWheel::MinTime() {
    int32_t next_setp = _bitmap.GetMinAfter(_cur_index);
    if (next_setp < 0) {
        return NO_TIMER;
    }

    return next_setp * _accuracy;
}

void TimerWheel::TimerRun(uint32_t step) {
    _cur_index += step;
    if (_cur_index > _size) {
        _cur_index -= _size;
    }

    auto bucket = _timer_wheel[_cur_index];
    if (bucket.size() > 0) {
        for (auto iter = bucket.begin(); iter != bucket.end();) {
            auto ptr = (iter)->lock();
            if (ptr) {
                ptr->OnTimer();
            }
            if (!ptr->GetAlways()) {
                iter = bucket.erase(iter);

            } else {
                ++iter;
            }
        }
    }
}