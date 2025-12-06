#include <string>
#include <gtest/gtest.h>
#include "common/util/time.h"

namespace quicx {
namespace common {
namespace {

TEST(time_utest, get_time1) {
    std::cout << UTCTimeSec() << std::endl;
    std::cout << UTCTimeMsec() << std::endl;
    std::cout << GetFormatTime() << std::endl;

    char buf[kFormatTimeBufSize] = {0};
    uint32_t size = kFormatTimeBufSize;
    GetFormatTime(buf, size);
    std::cout << buf << std::endl;
}


TEST(time_utest, get_time2) {
    std::string year = GetFormatTime(FormatTimeUnit::kYearFormat);
    EXPECT_EQ(year.length(), sizeof("xxxx") - 1);
    
    std::string month = GetFormatTime(FormatTimeUnit::kMonthFormat);
    EXPECT_EQ(month.length(), sizeof("xxxx-xx") - 1);

    std::string day = GetFormatTime(FormatTimeUnit::kDayFormat);
    EXPECT_EQ(day.length(), sizeof("xxxx-xx-xx") - 1);

    std::string hour = GetFormatTime(FormatTimeUnit::kHourFormat);
    EXPECT_EQ(hour.length(), sizeof("xxxx-xx-xx xx") - 1);

    std::string minute = GetFormatTime(FormatTimeUnit::kMinuteFormat);
    EXPECT_EQ(minute.length(), sizeof("xxxx-xx-xx xx:xx") - 1);

    std::string second = GetFormatTime(FormatTimeUnit::kSecondFormat);
    EXPECT_EQ(second.length(), sizeof("xxxx-xx-xx xx:xx:xx") - 1);

    std::string millisecond = GetFormatTime(FormatTimeUnit::kMillisecondFormat);
    EXPECT_EQ(millisecond.length(), sizeof("xxxx-xx-xx xx:xx:xx:xxx") - 1);
}

}
}
}