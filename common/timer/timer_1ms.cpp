#include "timer_1ms.h"

namespace quicx {

static const TIMER_CAPACITY __timer_accuracy = TC_1MS;  // 1 millisecond
static const TIMER_CAPACITY __timer_capacity = TC_50MS; // 50 millisecond

Timer1ms::Timer1ms() : _cur_index(0) {
    _timer_wheel.resize(__timer_capacity);
    _bitmap.Init(__timer_capacity);
}

Timer1ms::~Timer1ms() {

}

bool Timer1ms::AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time, bool always) {
    if (time >= __timer_capacity) {
        return false;
    }

    auto ptr = t.lock();
    if (!ptr) {
        return false;
    }

    if (ptr->IsInTimer()) {
        return true;
    }

    if (always) {
        ptr->SetAlways(__timer_accuracy);
    }

    time += _cur_index;
    if (time > __timer_capacity) {
        time /= __timer_capacity;
    }
    
    ptr->SetInterval(time);
    ptr->SetIndex(time);
    ptr->SetTimer();
    
    _timer_wheel[time].push_back(t);
    return _bitmap.Insert(time);
}

bool Timer1ms::RmTimer(std::weak_ptr<TimerSolt> t) {
    auto ptr = t.lock();
    if (!ptr) {
        return false;
    }

    auto index = ptr->GetIndex(__timer_accuracy);
    return _bitmap.Remove(index);
}

int32_t Timer1ms::MinTime() {
    int32_t next_setp = _bitmap.GetMinAfter(_cur_index);

    if (next_setp >= 0) {
        return next_setp - _cur_index;
    }

    if (_cur_index > 0) {
        next_setp = _bitmap.GetMinAfter(0);
    }
    
    if (next_setp >= 0) {
        return next_setp + __timer_capacity - _cur_index;
    }
    
    return NO_TIMER;
}

void Timer1ms::TimerRun(uint32_t time) {
    _cur_index += time;
    if (_cur_index >= __timer_capacity) {
        _cur_index %= __timer_capacity;
    }

    auto& bucket = _timer_wheel[_cur_index];
    if (bucket.size() > 0) {
        for (auto iter = bucket.begin(); iter != bucket.end();) {
            auto ptr = (iter)->lock();
            if (ptr && ptr->IsInTimer()) {
                ptr->OnTimer();
            }
            // remove from current bucket
            iter = bucket.erase(iter);
            _bitmap.Remove(ptr->GetIndex(__timer_accuracy));

            if (!ptr->IsAlways(__timer_accuracy) || !ptr->IsInTimer()) {
                continue;

            } else {
                // add to timer again
                uint16_t next_index = ptr->GetInterval() + _cur_index;
                if (next_index >= __timer_capacity) {
                    next_index = next_index % __timer_capacity;
                }
                AddTimerByIndex(ptr, next_index);
            }
        }
    }
}

void Timer1ms::AddTimerByIndex(std::weak_ptr<TimerSolt> t, uint8_t index) {
    auto ptr = t.lock();
    if (!ptr) {
        return;
    }

    ptr->SetIndex(index);
    _timer_wheel[index].push_back(t);
    _bitmap.Insert(index);
}

}