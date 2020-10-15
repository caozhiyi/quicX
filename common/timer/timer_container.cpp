#include <assert.h>
#include "timer_container.h"

namespace quicx {

TimerContainer::TimerContainer(std::shared_ptr<Timer> t, uint32_t accuracy, uint32_t capacity) :
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
        ptr->Set50MsAlways();
    }

    uint32_t index_1ms = time % _accuracy;
    uint32_t index_50ms = time / _accuracy;
    ptr->Set1MsIndex(index_1ms);
    ptr->Set50MsIndex(index_50ms);

    _timer_wheel[time].insert(t);
    return _bitmap.Insert(index_50ms);
}

bool TimerContainer::RmTimer(std::weak_ptr<TimerSolt> t) {
    auto ptr = t.lock();
    if (!ptr) {
        return false;
    }

    auto index_50ms = ptr->Get1MsIndex();
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
                _sub_timer->AddTimer(*iter, ptr->Get1MsIndex(), false);
            }
            if (!ptr->Is50MsAlways()) {
                iter = bucket.erase(iter);
                _bitmap.Remove(ptr->Get1MsIndex());

            } else {
                ++iter;
            }
        }
    }
    _sub_timer->TimerRun(time % _accuracy);
}

}