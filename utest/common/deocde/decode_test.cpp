#include <gtest/gtest.h>
#include "common/decode/decode.h"

TEST(decode_utest, EncodeVarint64_1) {
    uint64_t value = 1 << 5;
    char buf[5];
    char* ptr1 = quicx::EncodeVarint(buf, value);
    EXPECT_EQ(ptr1 - buf, 1);

    uint64_t value2 = 0;
    char* ptr2 = quicx::DecodeVirint(buf, buf+5, value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_2) {
    uint64_t value = 1 << 13;
    char buf[5];
    char* ptr1 = quicx::EncodeVarint(buf, value);
    EXPECT_EQ(ptr1 - buf, 2);

    uint64_t value2 = 0;
    char* ptr2 = quicx::DecodeVirint(buf, buf+5, value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_3) {
    uint64_t value = 1 << 29;
    char buf[5];
    char* ptr1 = quicx::EncodeVarint(buf, value);
    EXPECT_EQ(ptr1 - buf, 4);

    uint64_t value2 = 0;
    char* ptr2 = quicx::DecodeVirint(buf, buf+5, value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_4) {
    uint64_t value = (uint64_t)1 << 60;
    char buf[10];
    char* ptr1 = quicx::EncodeVarint(buf, value);
    EXPECT_EQ(ptr1 - buf, 8);

    uint64_t value2 = 0;
    char* ptr2 = quicx::DecodeVirint(buf, buf+5, value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}