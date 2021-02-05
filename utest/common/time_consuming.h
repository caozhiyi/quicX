#ifndef UTEST_COMMON_UTIL_OS_INFO
#define UTEST_COMMON_UTIL_OS_INFO

#include <chrono>
#include <string>

namespace quicx {

class TimeConsuming {
public:
    TimeConsuming(std::string name);
    ~TimeConsuming();

private:
    std::string _name;
    std::chrono::system_clock::time_point _start_time;
};

}

#endif