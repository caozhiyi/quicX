#include <gtest/gtest.h>
#include "common/decode/normal_decode.h"

TEST(normal_decode_utest, EncodeFixed_1) {
    uint32_t value = 1 << 5;
    char buf[5];
    char* ptr = quicx::EncodeFixed<uint32_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint32_t>(buf, buf+5, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_2) {
    uint32_t value = 1 << 13;
    char buf[5];
    char* ptr = quicx::EncodeFixed<uint32_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint32_t>(buf, buf+5, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_3) {
    uint32_t value = 1 << 20;
    char buf[5];
    char* ptr = quicx::EncodeFixed<uint32_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint32_t>(buf, buf+5, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_4) {
    uint32_t value = 1 << 27;
    char buf[5];
    char* ptr = quicx::EncodeFixed<uint32_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint32_t>(buf, buf+5, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_5) {
    uint64_t value = uint64_t(1) << 34;
    char buf[9];
    char* ptr = quicx::EncodeFixed<uint64_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint64_t));

    uint64_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint64_t>(buf, buf+9, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_6) {
    uint64_t value = uint64_t(1) << 41;
    char buf[9];
    char* ptr = quicx::EncodeFixed<uint64_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint64_t));

    uint64_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint64_t>(buf, buf+9, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_7) {
    uint64_t value = uint64_t(1) << 48;
    char buf[9];
    char* ptr = quicx::EncodeFixed<uint64_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint64_t));

    uint64_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint64_t>(buf, buf+9, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_8) {
    uint64_t value = uint64_t(1) << 55;
    char buf[9];
    char* ptr = quicx::EncodeFixed<uint64_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint64_t));

    uint64_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint64_t>(buf, buf+9, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_9) {
    uint8_t value = uint8_t(1) << 6;
    char buf[3];
    char* ptr = quicx::EncodeFixed<uint8_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint8_t));

    uint8_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint8_t>(buf, buf+3, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_10) {
    uint16_t value = 0x04;
    char buf[3];
    char* ptr = quicx::EncodeFixed<uint16_t>(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint16_t));

    uint16_t value2 = 0;
    char* ret = quicx::DecodeFixed<uint16_t>(buf, buf+3, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}