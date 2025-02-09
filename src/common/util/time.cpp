#include <chrono>
#include <thread>
#include "common/util/time.h"
#include "common/os/convert.h"

namespace quicx {
namespace common {

uint64_t UTCTimeSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t UTCTimeMsec() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string GetFormatTime(FormatTimeUnit unit) {
    char buf[kFormatTimeBufSize] = {0};
    uint32_t len = kFormatTimeBufSize;

    GetFormatTime(buf, len, unit);
    return std::move(std::string(buf, len));
}

void GetFormatTime(char* buf, uint32_t& len, FormatTimeUnit unit) {
    auto now_time = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now_time);
    
    int32_t millisecond = 0;
    if (unit == FormatTimeUnit::kMillisecondFormat) {
        auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(now_time.time_since_epoch()).count();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(now_time.time_since_epoch()).count();
        millisecond = (int32_t)(msec - (sec * 1000));
    }
    
    tm time;
    Localtime((uint64_t*)&now_time_t, (void*)&time);
    switch (unit)
    {
    case FormatTimeUnit::kMillisecondFormat:
        len = snprintf(buf, len, "%04d-%02d-%02d:%02d:%02d:%02d:%03d", 1900 + time.tm_year,  1 + time.tm_mon,  time.tm_mday,  time.tm_hour, time.tm_min, time.tm_sec, millisecond);
        break;
    case FormatTimeUnit::kSecondFormat:
        len = snprintf(buf, len, "%04d-%02d-%02d:%02d:%02d:%02d", 1900 + time.tm_year,  1 + time.tm_mon,  time.tm_mday,  time.tm_hour, time.tm_min, time.tm_sec);
        break;
    case FormatTimeUnit::kMinuteFormat:
        len = snprintf(buf, len, "%04d-%02d-%02d:%02d:%02d", 1900 + time.tm_year,  1 + time.tm_mon,  time.tm_mday,  time.tm_hour, time.tm_min);
        break;
    case FormatTimeUnit::kHourFormat:
        len = snprintf(buf, len, "%04d-%02d-%02d:%02d", 1900 + time.tm_year,  1 + time.tm_mon,  time.tm_mday,  time.tm_hour);
        break;
    case FormatTimeUnit::kDayFormat:
        len = snprintf(buf, len, "%04d-%02d-%02d", 1900 + time.tm_year,  1 + time.tm_mon,  time.tm_mday);
        break;
    case FormatTimeUnit::kMonthFormat:
        len = snprintf(buf, len, "%04d-%02d", 1900 + time.tm_year,  1 + time.tm_mon);
        break;
    case FormatTimeUnit::kYearFormat:
        len = snprintf(buf, len, "%04d", 1900 + time.tm_year);
        break;
    default:
        // default FormatTimeUnit::kMillisecondFormat
        len = snprintf(buf, len, "%04d-%02d-%02d:%02d:%02d:%02d:%03d", 1900 + time.tm_year,  1 + time.tm_mon,  time.tm_mday,  time.tm_hour, time.tm_min, time.tm_sec, millisecond);
        break;
    }
}


void Sleep(uint32_t interval) {
    std::this_thread::sleep_for(std::chrono::milliseconds(interval));
}


}
}