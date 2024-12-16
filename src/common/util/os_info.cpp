#include <atomic>
#include "common/util/os_info.h"

namespace quicx {
namespace common {

bool IsBigEndian() {
    static union temp_endian {
        int32_t i;
        char c;
    } temp;
    temp.i = 1;

    return temp.c != 1;
}

}
}