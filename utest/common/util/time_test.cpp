#include <string>
#include <gtest/gtest.h>
#include "common/util/time.h"
#include "utest/common/time_consuming.h"

namespace quicx {
namespace common {
namespace {

TEST(time_utest, get_time1) {
    std::cout << UTCTimeSec() << std::endl;
    std::cout << UTCTimeMsec() << std::endl;
    std::cout << GetFormatTime() << std::endl;

    char buf[__format_time_buf_size] = {0};
    uint32_t size = __format_time_buf_size;
    GetFormatTime(buf, size);
    std::cout << buf << std::endl;
}


TEST(time_utest, get_time2) {
    std::string year = GetFormatTime(FTU_YEAR);
    EXPECT_EQ(year.length(), sizeof("xxxx") - 1);
    
    std::string month = GetFormatTime(FTU_MONTH);
    EXPECT_EQ(month.length(), sizeof("xxxx-xx") - 1);

    std::string day = GetFormatTime(FTU_DAY);
    EXPECT_EQ(day.length(), sizeof("xxxx-xx-xx") - 1);

    std::string hour = GetFormatTime(FTU_HOUR);
    EXPECT_EQ(hour.length(), sizeof("xxxx-xx-xx xx") - 1);

    std::string minute = GetFormatTime(FTU_MINUTE);
    EXPECT_EQ(minute.length(), sizeof("xxxx-xx-xx xx:xx") - 1);

    std::string second = GetFormatTime(FTU_SECOND);
    EXPECT_EQ(second.length(), sizeof("xxxx-xx-xx xx:xx:xx") - 1);

    std::string millisecond = GetFormatTime(FTU_MILLISECOND);
    EXPECT_EQ(millisecond.length(), sizeof("xxxx-xx-xx xx:xx:xx:xxx") - 1);
}

}
}
}