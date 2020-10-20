#ifndef UTEST_COMMON_TIMER_TIMER_SOLT
#define UTEST_COMMON_TIMER_TIMER_SOLT

#include "common/timer/timer_solt.h"

class TimerSoltIns: public quicx::TimerSolt {
public:
    void OnTimer() {}
};

#endif