#ifdef WIN32

#include "common/os/convert.h"
#include <time.h>

namespace quicx {
namespace common {

void Localtime(const uint64_t* time, void* out_tm) {
    ::localtime_s((tm*)out_tm, (time_t*)time);
}

}  // namespace common
}  // namespace quicx

#endif