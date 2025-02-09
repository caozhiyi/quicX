#ifndef COMMON_UTIL_TIME
#define COMMON_UTIL_TIME

#include <string>
#include <cstdint>

namespace quicx {
namespace common {

static const uint8_t kFormatTimeBufSize = sizeof("xxxx-xx-xx:xx:xx:xx:xxx");

enum FormatTimeUnit {
    kYearFormat        = 1, // 2021
    kMonthFormat       = 2, // 2021-03
    kDayFormat         = 3, // 2021-03-16
    kHourFormat        = 4, // 2021-03-16:10
    kMinuteFormat      = 5, // 2021-03-16:10:03
    kSecondFormat      = 6, // 2021-03-16:10:03:33
    kMillisecondFormat = 7, // 2021-03-16:10:03:33:258
};

enum TimeUnit {
    kMillisecond = 1,
    kSecond      = kMillisecond * 1000,
    kMinute      = kSecond * 60,
    kHour        = kMinute * 60,
    kDay         = kHour * 24,
};

// get format time string [xxxx-xx-xx xx:xx:xx]
std::string GetFormatTime(FormatTimeUnit unit = FormatTimeUnit::kMillisecondFormat);
// get format time string as [xxxx-xx-xx xx:xx:xx]
void GetFormatTime(char* buf, uint32_t& len, FormatTimeUnit unit = FormatTimeUnit::kMillisecondFormat);

// get utc time
uint64_t UTCTimeSec();
uint64_t UTCTimeMsec();

// sleep interval milliseconds
void Sleep(uint32_t interval);

}
}

#endif