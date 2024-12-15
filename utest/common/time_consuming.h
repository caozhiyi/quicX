#ifndef UTEST_COMMON_UTIL_OS_INFO
#define UTEST_COMMON_UTIL_OS_INFO

#include <chrono>
#include <string>

namespace quicx {
namespace common {

class TimeConsuming {
public:
    TimeConsuming(std::string name);
    ~TimeConsuming();

private:
    std::string name_;
    std::chrono::system_clock::time_point start_time_;
};

}
}

#endif