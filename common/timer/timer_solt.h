#ifndef COMMON_TIMER_TIMER_SOLT
#define COMMON_TIMER_TIMER_SOLT

#include <stdint.h>
#include <unordered_map>
#include "timer_interface.h"

namespace quicx {

// timer task
class TimerSolt {
public:

    enum TIMER_SOLT_FLAG {
        TSF_INDEX_MASK = 11 << 6,
        TSF_ALWAYS     = 1 << 7,
        TSF_REMOVED    = 1 << 6,
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

    void SetTimer();
    void RmTimer();
    bool IsInTimer();

private:
    void SetIndex(uint32_t pos, uint8_t index);

private:
    static std::unordered_map<uint32_t, uint8_t> _index_map;
    struct {
        union {
            uint8_t _index[4];
        } _info;
        uint32_t _index;
    } _index;
};

}

#endif