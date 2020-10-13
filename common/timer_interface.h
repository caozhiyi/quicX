#ifndef COMMON_TIMER_INTERFACE
#define COMMON_TIMER_INTERFACE

#include <memory>

// timer task
class TimerSolt {
public:
    // timer out call back
    virtual void OnTimer() = 0;

    void SetMsIndex(uint8_t i) { _ms_index = i; }
    uint8_t GetMsIndex() { return _ms_index; }

    void SetSecIndex(uint8_t i) { _sec_index = i; }
    uint8_t GetSecIndex() { return _sec_index; }

    void SetMinIndex(uint8_t i) { _min_index = i; }
    uint8_t GetMinIndex() { return _min_index; }

    void SetAlways(uint8_t i) { _always = i; }
    uint8_t GetAlways() { return _always; }

    private:
    struct {
        union {
            uint8_t _ms_index;
            uint8_t _sec_index;
            uint8_t _min_index;
            uint8_t _always;
        };
        uint32_t _index;
    };
};

enum TimerCode {
    NO_TIMER = -1 // don't have timer
};

class Timer {
public:
    Timer() {}
    ~Timer() {}

    virtual bool AddTimer(std::weak_ptr<TimerSolt> t, uint32_t time) = 0;
    virtual bool RmTimer(std::weak_ptr<TimerSolt> t) = 0;
    virtual bool RmTimer(std::weak_ptr<TimerSolt> t, uint32_t index) = 0;

    // get min next timer out time
    // return >= 0 : the next time
    // < 0 : has no timer
    virtual int32_t MinTime() = 0;

    virtual void TimerRun(uint32_t step) = 0;
};

#endif