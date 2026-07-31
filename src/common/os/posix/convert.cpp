#if ((defined __linux__) || (defined __APPLE__))

#include "common/os/convert.h"
#include <time.h>

namespace quicx {
namespace common {

void Localtime(const uint64_t* time, void* out_tm) {
    ::localtime_r((time_t*)time, (tm*)out_tm);
}

}  // namespace common
}  // namespace quicx

#endif