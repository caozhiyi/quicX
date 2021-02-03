#ifndef UTEST_COMMON_UTIL_OS_INFO
#define UTEST_COMMON_UTIL_OS_INFO

#include <chrono>

namespace quicx {

class TimeConsuming {
public:
    TimeConsuming();
    ~TimeConsuming();

private:
    time_t _clock;
};

}

#endif