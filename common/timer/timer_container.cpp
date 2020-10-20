#include <assert.h>
#include "timer_container.h"

namespace quicx {

TimerContainer::TimerContainer(std::shared_ptr<Timer> t, TIMER_CAPACITY accuracy, TIMER_CAPACITY capacity) :
    _sub_timer(t),
    _cur_index(0),
    _accuracy(accuracy),
    _capacity(capacity) {
    assert(t);
    auto size = capacity/accuracy;
    _timer_wheel.resize(size);
    _bitmap.Init(size);
}
TimerContainer::~TimerContainer() {

}

bool TimerContainer::AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time, bool always) {
    if (time >= _capacity) {
        return false;
    }

    if (time < _accuracy) {
        return _sub_timer->AddTimer(t, time, always);
    }

    auto ptr = t.lock();
    if (!ptr) {
        return false;
    }

    if (ptr->IsInTimer()) {
        return true;
    }

    if (always) {
        ptr->SetAlways(_capacity);
    }

    time += _cur_index * _accuracy;
    if (time > _capacity) {
        time /= _capacity;
    }
    uint32_t index = ptr->SetIndex(time);
    ptr->SetTimer();

    _timer_wheel[index].push_back(t);
    return _bitmap.Insert(index);
}

bool TimerContainer::RmTimer(std::weak_ptr<TimerSolt> t) {
    auto ptr = t.lock();
    if (!ptr) {
        return false;
    }

    ptr->RmTimer();
    auto index_50ms = ptr->GetIndex(_capacity);
    return _bitmap.Remove(index_50ms) && _sub_timer->RmTimer(t);
}

int32_t TimerContainer::MinTime() {
    int32_t next_setp = _bitmap.GetMinAfter(_cur_index);
    if (next_setp < 0) {
        return NO_TIMER;
    }

    return (next_setp - _cur_index) * _accuracy + _sub_timer->MinTime();
}

void TimerContainer::TimerRun(uint32_t time) {
    _cur_index += time / _accuracy;
    if (_cur_index > _capacity) {
        _cur_index %= _capacity;
    }

    auto bucket = _timer_wheel[_cur_index];
    if (bucket.size() > 0) {
        for (auto iter = bucket.begin(); iter != bucket.end();) {
            auto ptr = (iter)->lock();
            if (ptr && ptr->IsInTimer()) {
                _sub_timer->AddTimerByIndex(*iter, ptr->GetIndex(_accuracy));
            }
            // remove from current bucket
            iter = bucket.erase(iter);
            _bitmap.Remove(ptr->GetIndex(_capacity));
            if (!ptr->IsAlways(_capacity) || !ptr->IsInTimer()) {
               continue;

            } else {
                // add to timer again
                AddTimer(ptr, ptr->GetInterval(), true);
            }
        }
    }
    _sub_timer->TimerRun(time % _accuracy);
}


void TimerContainer::AddTimerByIndex(std::weak_ptr<TimerSolt> t, uint8_t index) {
    auto ptr = t.lock();
    if (!ptr) {
        return;
    }
    _timer_wheel[index].push_back(t);
    _bitmap.Insert(index);
}

}