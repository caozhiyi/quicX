#include "timer_solt.h"

namespace quicx {

std::unordered_map<uint32_t, uint8_t> TimerSolt::_index_map;

TimerSolt::TimerSolt() {  
    _index._index = 0;
    _index_map[TC_1MS]  =  0;
    _index_map[TC_50MS] =  1;
    _index_map[TC_1SEC] =  2;
    _index_map[TC_1MIN] =  3;
}

TimerSolt::~TimerSolt() {

}

uint8_t TimerSolt::GetIndex(TIMER_CAPACITY tc) {
    auto index = _index_map[tc];
    return _index._info._index[index] &= ~TSF_INDEX_MASK;
}

uint8_t TimerSolt::SetIndex(uint32_t time) {
    if (time > TC_1HOUR) {
        return 0;
    }
    uint8_t ret = 0;
    if (time > TC_1MIN) {
        uint8_t index = time / TC_1MIN;
        time = time % TC_1MIN;
        SetIndex(3, index);
        ret = index;
    } 
    
    if (time > TC_1SEC) {
        uint8_t index = time / TC_1SEC;
        time = time % TC_1SEC;
        SetIndex(2, index);
        ret = index;
    }

    if (time > TC_50MS) {
        uint8_t index = time / TC_50MS;
        time = time % TC_50MS;
        SetIndex(1, index);
        ret = index;
    }
    
    if (time > 0) {
        uint8_t index = time;
        SetIndex(0, index);
        ret = index;
    }
    return ret;
}

void TimerSolt::SetAlways(TIMER_CAPACITY tc) {
    auto index = _index_map[tc];
    _index._info._index[index] |= TSF_ALWAYS;
}

void TimerSolt::CancelAlways(TIMER_CAPACITY tc) {
    auto index = _index_map[tc];
    _index._info._index[index] &= ~TSF_ALWAYS;
}

bool TimerSolt::IsAlways(TIMER_CAPACITY tc) {
    auto index = _index_map[tc];
    return _index._info._index[index] & TSF_ALWAYS;
}

void TimerSolt::SetIndex(uint32_t pos, uint8_t index) {
    // clear current index
    _index._info._index[pos] &= TSF_INDEX_MASK;
    _index._info._index[pos] |= index; 
}

void TimerSolt::SetTimer() {
    _index._info._index[3] |= TSF_REMOVED;
}

void TimerSolt::RmTimer() {
    _index._info._index[3] &= ~TSF_REMOVED;
}

bool TimerSolt::IsInTimer() {
    return _index._info._index[3] & TSF_REMOVED;
}

}