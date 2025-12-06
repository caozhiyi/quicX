#include <chrono>
#include <gtest/gtest.h>
#include "../time_consuming.h"
#include "common/util/os_info.h"

namespace quicx {
namespace common {
namespace {

TEST(os_info_utest, IsBigEndian_time) {
    TimeConsuming tc("IsBigEndian_time");
    for (uint32_t i = 0; i < 1000000; i++) {
        EXPECT_FALSE(IsBigEndian());
    }
}

TEST(os_info_utest, IsBigEndian) {
    bool is = IsBigEndian();
}

}
}
}