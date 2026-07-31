#include "common/util/os_info.h"
#include <gtest/gtest.h>
#include <chrono>
#include "../time_consuming.h"

namespace quicx {
namespace common {
namespace {

TEST(OsInfoTest, IsBigEndian_time) {
    TimeConsuming tc("IsBigEndian_time");
    for (uint32_t i = 0; i < 1000000; i++) {
        EXPECT_FALSE(IsBigEndian());
    }
}

TEST(OsInfoTest, IsBigEndian) {
    bool is = IsBigEndian();
}

}  // namespace
}  // namespace common
}  // namespace quicx