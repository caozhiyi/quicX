#ifndef COMMON_TIMER_TIMER_SOLT
#define COMMON_TIMER_TIMER_SOLT

#include <stdint.h>
#include <unordered_map>
#include "timer_interface.h"

namespace quicx {

// timer task
class TimerSolt {
public:
    enum ALWAYS_FLAG {
        AF_1MS  = 1 << 7,
        AF_50MS = 1 << 15,
        AF_SEC  = 1 << 23,
        AF_MIN  = 1 << 31,
    };

    TimerSolt();
    ~TimerSolt();
    // timer out call back
    virtual void OnTimer() = 0;

    uint8_t GetIndex(TIMER_CAPACITY tc);
    uint8_t SetIndex(uint32_t time);

    void SetAlways(TIMER_CAPACITY tc);
    void CancelAlways(TIMER_CAPACITY tc);
    bool IsAlways(TIMER_CAPACITY tc);

    void Clear() { _index._index = 0; }

private:
    void SetIndex(uint32_t pos, uint8_t index, ALWAYS_FLAG af);

private:
    static std::unordered_map<TIMER_CAPACITY, std::pair<ALWAYS_FLAG, uint8_t>> _index_map;
    struct {
        union {
            uint8_t _index[4];
        } _info;
        uint32_t _index;
    } _index;
};

}

#endif