#include <iostream>
#include <gtest/gtest.h>
#include "common/network/address.h"

namespace quicx {
namespace common {
namespace {

TEST(address_utest, asstrng) {
    Address addr("127.0.0.1", 8080);
    EXPECT_EQ(addr.AsString(), "127.0.0.1:8080");
}

TEST(address_utest, compare) {
    Address addr1("127.0.0.1", 8080);
    Address addr2("127.0.0.1", 8080);
    Address addr3("127.0.0.2", 8080);
    EXPECT_TRUE(addr1 == addr2);
    EXPECT_FALSE(addr1 == addr3);
}

TEST(address_utest, iostream) {
    Address addr("127.0.0.1", 8080);
    std::cout << addr << std::endl;
}

}
}
}