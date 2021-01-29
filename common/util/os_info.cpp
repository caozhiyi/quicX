#include "os_info.h"

namespace quicx {

bool IsBigEndian() {
    static bool do_once =false;
    static bool is_big_endian;
    if (!do_once) {
        union temp_endian {
            int i;
            char c;
        } temp;
        temp.i = 1;

        is_big_endian = temp.c == 1;
        do_once = true;
    }

    return is_big_endian;
}

}