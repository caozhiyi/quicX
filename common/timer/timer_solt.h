#ifndef COMMON_TIMER_SOLT
#define COMMON_TIMER_SOLT

#include <stdint.h>

namespace quicx {

// timer task
class TimerSolt {
public:
    TimerSolt() {  _index._index = 0; }
    ~TimerSolt() {}
    // timer out call back
    virtual void OnTimer() = 0;

    void Set1MsIndex(uint8_t i) { 
        _index._info._1ms_index &= AF_1MS; 
        _index._info._1ms_index |= i; 
    }
    uint8_t Get1MsIndex() { 
        return _index._info._1ms_index &= ~AF_1MS; 
    }

    void Set50MsIndex(uint8_t i) { 
        _index._info._50ms_index &= AF_50MS;
        _index._info._50ms_index |= i; 
    }
    uint8_t Get50MsIndex() { 
        return _index._info._50ms_index &= ~AF_50MS;
    }

    void SetSecIndex(uint8_t i) { 
        _index._info._sec_index &= AF_SEC;
        _index._info._sec_index |= i; 
    }
    uint8_t GetSecIndex() { 
        return _index._info._sec_index &= ~AF_SEC;
    }

    void SetMinIndex(uint8_t i) { 
        _index._info._min_index &= AF_MIN;
        _index._info._min_index |= i;
    }
    uint8_t GetMinIndex() { 
        return _index._info._min_index &= ~AF_MIN;
    }

    void Set1MsAlways() { _index._info._1ms_index |= AF_1MS; }
    void Cancel1MsAlways() { _index._info._1ms_index &= ~AF_1MS; }
    bool Is1MsAlways() { return _index._info._1ms_index & AF_1MS; }

    void Set50MsAlways() { _index._info._50ms_index |= AF_50MS; }
    void Cancel50MsAlways() { _index._info._50ms_index &= ~AF_50MS; }
    bool Is50MsAlways() { return _index._info._50ms_index & AF_50MS; }

    void SetSecMsAlways() { _index._info._sec_index |= AF_SEC; }
    void CancelSecMsAlways() { _index._info._sec_index &= ~AF_SEC; }
    bool IsSecMsAlways() { return _index._info._sec_index & AF_SEC; }

    void SetMinMsAlways() { _index._info._min_index |= AF_MIN; }
    void CancelMinMsAlways() { _index._info._min_index &= ~AF_MIN; }
    bool IsMinMsAlways() { return _index._info._min_index & AF_MIN; }

    void Clear() { _index._index = 0; }

    private:
    enum ALWAYS_FLAG {
        AF_1MS  = 1 << 7,
        AF_50MS = 1 << 15,
        AF_SEC  = 1 << 23,
        AF_MIN  = 1 << 31,
    };
    struct {
        union {
            uint8_t _1ms_index;
            uint8_t _50ms_index;
            uint8_t _sec_index;
            uint8_t _min_index;
        } _info;
        uint32_t _index;
    } _index;
};

}

#endif