#ifndef COMMON_OS_CONVERT
#define COMMON_OS_CONVERT

#include <cstdint>

namespace quicx {

void Localtime(const uint64_t* time, void* out_tm);

}

#endif