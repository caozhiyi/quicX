#include "timer_1ms.h"

namespace quicx {

static const TIMER_CAPACITY __timer_accuracy = TC_1MS;  // 1 millisecond
static const TIMER_CAPACITY __timer_capacity = TC_50MS; // 1 millisecond

Timer1ms::Timer1ms() {
    _timer_wheel.resize(__timer_capacity);
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

    if (always) {
        ptr->SetAlways(__timer_accuracy);
    }

    ptr->SetIndex(time);
    _timer_wheel[time].insert(t);
    return _bitmap.Insert(time);
}

bool Timer1ms::RmTimer(std::weak_ptr<TimerSolt> t) {
    auto ptr = t.lock();
    if (!ptr) {
        return false;
    }

    auto index = ptr->GetIndex(__timer_capacity);
    _timer_wheel[index].erase(t);
    return _bitmap.Remove(index);
}

int32_t Timer1ms::MinTime() {
    int32_t next_setp = _bitmap.GetMinAfter(_cur_index);
    if (next_setp < 0) {
        return NO_TIMER;
    }

    return next_setp * __timer_accuracy;
}

void Timer1ms::TimerRun(uint32_t time) {
    _cur_index += time;
    if (_cur_index > __timer_capacity) {
        _cur_index %= __timer_capacity;
    }

    auto bucket = _timer_wheel[_cur_index];
    if (bucket.size() > 0) {
        for (auto iter = bucket.begin(); iter != bucket.end();) {
            auto ptr = (iter)->lock();
            if (ptr) {
                ptr->OnTimer();
            }
            if (!ptr->IsAlways(__timer_capacity)) {
                iter = bucket.erase(iter);
                _bitmap.Remove(ptr->GetIndex(__timer_accuracy));

            } else {
                ++iter;
            }
        }
    }
}

void Timer1ms::AddTimer(std::weak_ptr<TimerSolt> t, uint8_t index) {
    auto ptr = t.lock();
    if (!ptr) {
        return;
    }
    _timer_wheel[index].insert(t);
    _bitmap.Insert(index);
}

}