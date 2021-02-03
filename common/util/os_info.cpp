#include "os_info.h"
#include <atomic>

namespace quicx {

bool IsBigEndian() {
    union temp_endian {
        int i;
        char c;
    } temp;
    temp.i = 1;

    return temp.c == 1;
}

}