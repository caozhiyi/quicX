#include <gtest/gtest.h>
#include "common/decode/decode.h"

TEST(normal_decode_utest, EncodeFixed_1) {
    uint32_t value = 1 << 5;
    char buf[5];
    char* ptr = quicx::FixedEncodeUint32(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    char* ret = quicx::FixedDecodeUint32(buf, buf+5, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_2) {
    uint32_t value = 1 << 13;
    char buf[5];
    char* ptr = quicx::FixedEncodeUint32(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    char* ret = quicx::FixedDecodeUint32(buf, buf+5, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_3) {
    uint32_t value = 1 << 20;
    char buf[5];
    char* ptr = quicx::FixedEncodeUint32(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    char* ret = quicx::FixedDecodeUint32(buf, buf+5, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_4) {
    uint32_t value = 1 << 27;
    char buf[5];
    char* ptr = quicx::FixedEncodeUint32(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    char* ret = quicx::FixedDecodeUint32(buf, buf+5, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_9) {
    uint8_t value = uint8_t(1) << 6;
    char buf[3];
    char* ptr = quicx::FixedEncodeUint8(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint8_t));

    uint8_t value2 = 0;
    char* ret = quicx::FixedDecodeUint8(buf, buf+3, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_10) {
    uint16_t value = 0x04;
    char buf[3];
    char* ptr = quicx::FixedEncodeUint16(buf, value);
    EXPECT_EQ(ptr - buf, sizeof(uint16_t));

    uint16_t value2 = 0;
    char* ret = quicx::FixedDecodeUint16(buf, buf+3, value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}