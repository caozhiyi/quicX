#if ((defined __linux__) || (defined __APPLE__)) 

#include <time.h>
#include "../convert.h"

namespace quicx {

void Localtime(const uint64_t* time, void* out_tm) {
    ::localtime_r((time_t*)time, (tm*)out_tm);
}

}

#endif