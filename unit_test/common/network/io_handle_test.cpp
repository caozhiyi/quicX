#include <gtest/gtest.h>
#include "common/log/log.h"
#include "common/network/io_handle.h"

namespace quicx {
namespace common {
namespace {

TEST(LookupAddressTest, lookup) {
    Address addr;
    EXPECT_TRUE(LookupAddress("baidu.com", addr));
    LOG_INFO("addr: %s", addr.AsString().c_str());
}

}
}
}