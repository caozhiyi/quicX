#ifndef COMMON_UTIL_TIME
#define COMMON_UTIL_TIME

#include <string>

namespace quicx {

static const uint8_t __format_time_buf_size = sizeof("xxxx-xx-xx xx:xx:xx:xxx");

uint64_t UTCTimeSec();
uint64_t UTCTimeMsec();

//return xxxx-xx-xx xx:xx:xx
std::string GetFormatTime();
//return xxxx-xx-xx xx:xx:xx
void GetFormatTime(char* buf, uint32_t len);

}

#endif