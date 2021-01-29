#include <gtest/gtest.h>
#include "common/util/os_info.h"

TEST(os_info_utest, IsBigEndian) {
    bool is = quicx::IsBigEndian();
}