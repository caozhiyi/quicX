#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstring>
#include "time.h"
#include <time.h>

namespace quicx {

uint64_t UTCTimeSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

uint64_t UTCTimeMsec() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string GetFormatTime() {
    char buf[16] = {0};
    GetFormatTime(buf, 16);
    return std::move(std::string(buf));
}

void GetFormatTime(char* buf, int len) {
    auto now_time = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now_time);
    auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(now_time.time_since_epoch()).count();
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(now_time.time_since_epoch()).count();

    tm time;
    ::localtime_r(&now_time_t, &time);
    sprintf(buf, "%d-%d-%d %d:%d:%d:%d", 1900 + time.tm_year,  time.tm_mon,  time.tm_mday,  time.tm_hour, time.tm_min, time.tm_sec, (int)(msec - (sec * 1000)));
}

}