#include <chrono>
#include <gtest/gtest.h>
#include "common/util/time.h"
#include "../time_consuming.h"

TEST(time_utest, get_time1) {
    std::cout << quicx::UTCTimeSec() << std::endl;
    std::cout << quicx::UTCTimeMsec() << std::endl;
    std::cout << quicx::GetFormatTime() << std::endl;

    char buf[32] = {0};
    quicx::GetFormatTime(buf, 32);
    std::cout << buf << std::endl;
}