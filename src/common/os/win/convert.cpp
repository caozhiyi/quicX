#ifdef WIN32

#include <time.h>
#include "common/os/convert.h"

namespace quicx {
namespace common {

void Localtime(const uint64_t* time, void* out_tm) {
    ::localtime_r((tm*)out_tm, (time_t*)time)
}

}
}

#endif