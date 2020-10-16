#include "timer_solt.h"

namespace quicx {

std::unordered_map<TIMER_CAPACITY, std::pair<TimerSolt::ALWAYS_FLAG, uint8_t>> TimerSolt::_index_map;

TimerSolt::TimerSolt() {  
    _index._index = 0;
    _index_map[TC_1MS]  = std::make_pair(AF_1MS, 0);
    _index_map[TC_50MS] = std::make_pair(AF_50MS, 1);
    _index_map[TC_1SEC] = std::make_pair(AF_SEC, 2);
    _index_map[TC_1MIN] = std::make_pair(AF_MIN, 3);
}

TimerSolt::~TimerSolt() {

}

uint8_t TimerSolt::GetIndex(TIMER_CAPACITY tc) {
    auto index = _index_map[tc];
    return _index._info._index[index.second] &= ~index.first;
}

uint8_t TimerSolt::SetIndex(uint32_t time) {
    if (time > TC_1HOUR) {
        return 0;
    }
    uint8_t ret = 0;
    if (time > TC_1MIN) {
        uint8_t index = time / TC_1MIN;
        time = time % TC_1MIN;
        SetIndex(3, index, AF_MIN);
        ret = index;
    } 
    
    if (time > TC_1SEC) {
        uint8_t index = time / TC_1SEC;
        time = time % TC_1SEC;
        SetIndex(2, index, AF_SEC);
        ret = index;
    }

    if (time > TC_50MS) {
        uint8_t index = time / TC_50MS;
        time = time % TC_50MS;
        SetIndex(1, index, AF_50MS);
        ret = index;
    }
    
    if (time > 0) {
        uint8_t index = time;
        SetIndex(0, index, AF_1MS);
        ret = index;
    }
    return ret;
}

void TimerSolt::SetAlways(TIMER_CAPACITY tc) {
    auto index = _index_map[tc];
    _index._info._index[index.second] |= index.first;
}

void TimerSolt::CancelAlways(TIMER_CAPACITY tc) {
    auto index = _index_map[tc];
    _index._info._index[index.second] &= ~index.first;
}

bool TimerSolt::IsAlways(TIMER_CAPACITY tc) {
    auto index = _index_map[tc];
    return _index._info._index[index.second] & index.first;
}

void TimerSolt::SetIndex(uint32_t pos, uint8_t index, ALWAYS_FLAG af) {
    _index._info._index[pos] &= af; 
    _index._info._index[pos] |= index; 
}

}