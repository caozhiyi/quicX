#include <gtest/gtest.h>
#include "common/decode/decode.h"

namespace quicx {
namespace common {
namespace {

TEST(decode_utest, EncodeVarint64_1) {
    uint64_t value = 1 << 5;
    uint8_t buf[5];
    uint8_t* ptr1 = EncodeVarint(buf, buf+sizeof(buf), value);
    EXPECT_EQ(ptr1 - buf, 1);

    uint64_t value2 = 0;
    const uint8_t* ptr2 = DecodeVarint(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_2) {
    uint64_t value = 1 << 13;
    uint8_t buf[5];
    uint8_t* ptr1 = EncodeVarint(buf, buf+sizeof(buf),value);
    EXPECT_EQ(ptr1 - buf, 2);

    uint64_t value2 = 0;
    const uint8_t* ptr2 = DecodeVarint(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_3) {
    uint64_t value = 1 << 29;
    uint8_t buf[5];
    uint8_t* ptr1 = EncodeVarint(buf, buf+sizeof(buf),value);
    EXPECT_EQ(ptr1 - buf, 4);

    uint64_t value2 = 0;
    const uint8_t* ptr2 = DecodeVarint(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_4) {
    uint64_t value = (uint64_t)1 << 60;
    uint8_t buf[10];
    uint8_t* ptr1 = EncodeVarint(buf, buf+sizeof(buf), value);
    EXPECT_EQ(ptr1 - buf, 8);

    uint64_t value2 = 0;
    const uint8_t* ptr2 = DecodeVarint(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

}
}
}