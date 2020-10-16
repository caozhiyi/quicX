#include <assert.h>
#include "timer_container.h"

namespace quicx {

TimerContainer::TimerContainer(std::shared_ptr<Timer> t, TIMER_CAPACITY accuracy, TIMER_CAPACITY capacity) :
    _sub_timer(t),
    _accuracy(accuracy),
    _capacity(capacity) {
    assert(t);
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

    if (always) {
        ptr->SetAlways(_capacity);
    }

    uint32_t index = ptr->SetIndex(time);

    _timer_wheel[time].insert(t);
    return _bitmap.Insert(index);
}

bool TimerContainer::RmTimer(std::weak_ptr<TimerSolt> t) {
    auto ptr = t.lock();
    if (!ptr) {
        return false;
    }

    auto index_50ms = ptr->GetIndex(_capacity);
    _timer_wheel[index_50ms].erase(t);
    return _bitmap.Remove(index_50ms) && _sub_timer->RmTimer(t);
}

int32_t TimerContainer::MinTime() {
    int32_t next_setp = _bitmap.GetMinAfter(_cur_index);
    if (next_setp < 0) {
        return NO_TIMER;
    }

    return next_setp * _accuracy + _sub_timer->MinTime();
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
            if (ptr) {
                _sub_timer->AddTimer(*iter, ptr->GetIndex(_accuracy));
            }
            if (!ptr->IsAlways(_capacity)) {
                iter = bucket.erase(iter);
                _bitmap.Remove(ptr->GetIndex(_capacity));

            } else {
                ++iter;
            }
        }
    }
    _sub_timer->TimerRun(time % _accuracy);
}


void TimerContainer::AddTimer(std::weak_ptr<TimerSolt> t, uint8_t index) {
    auto ptr = t.lock();
    if (!ptr) {
        return;
    }
    _timer_wheel[index].insert(t);
    _bitmap.Insert(index);
}

}