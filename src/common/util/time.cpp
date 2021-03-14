#include <chrono>
#include "time.h"
#include "common/os/convert.h"

namespace quicx {

uint64_t UTCTimeSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t UTCTimeMsec() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string GetFormatTime() {
    static const uint8_t __buf_size = sizeof("xxxx-xx-xx xx:xx:xx:xxx");

    char buf[__buf_size] = {0};
    GetFormatTime(buf, __buf_size);
    return std::move(std::string(buf));
}

void GetFormatTime(char* buf, int len) {
    auto now_time = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now_time);
    auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(now_time.time_since_epoch()).count();
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(now_time.time_since_epoch()).count();

    tm time;
    Localtime((uint64_t*)&now_time_t, (void*)&time);
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d:%03d", 1900 + time.tm_year,  1 + time.tm_mon,  time.tm_mday,  time.tm_hour, time.tm_min, time.tm_sec, (int)(msec - (sec * 1000)));
}

}