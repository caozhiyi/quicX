#ifndef COMMON_UTIL_TIME
#define COMMON_UTIL_TIME

#include <string>

namespace quicx {

uint64_t UTCTimeSec();
uint64_t UTCTimeMsec();

//return xxxx xx xx-xx:xx:xx
std::string GetFormatTime();
//return xxxx xx xx-xx:xx:xx
void GetFormatTime(char* buf, int len);

}

#endif