#include <gtest/gtest.h>
#include "common/decode/decode.h"
namespace quicx {
namespace common {
namespace {

TEST(normal_decode_utest, EncodeFixed_1) {
    uint32_t value = 1 << 5;
    uint8_t buf[5];
    uint8_t* ptr = FixedEncodeUint32(buf, buf+sizeof(buf), value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    const uint8_t* ret = FixedDecodeUint32(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_2) {
    uint32_t value = 1 << 13;
    uint8_t buf[5];
    uint8_t* ptr = FixedEncodeUint32(buf, buf+sizeof(buf), value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    const uint8_t* ret = FixedDecodeUint32(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_3) {
    uint32_t value = 1 << 20;
    uint8_t buf[5];
    uint8_t* ptr = FixedEncodeUint32(buf, buf+sizeof(buf), value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    const uint8_t* ret = FixedDecodeUint32(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_4) {
    uint32_t value = 1 << 27;
    uint8_t buf[5];
    uint8_t* ptr = FixedEncodeUint32(buf, buf+sizeof(buf), value);
    EXPECT_EQ(ptr - buf, sizeof(uint32_t));

    uint32_t value2 = 0;
    const uint8_t* ret = FixedDecodeUint32(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_9) {
    uint8_t value = uint8_t(1) << 6;
    uint8_t buf[3];
    uint8_t* ptr = FixedEncodeUint8(buf, buf+sizeof(buf), value);
    EXPECT_EQ(ptr - buf, sizeof(uint8_t));

    uint8_t value2 = 0;
    const uint8_t* ret = FixedDecodeUint8(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

TEST(normal_decode_utest, EncodeFixed_10) {
    uint16_t value = 0x04;
    uint8_t buf[3];
    uint8_t* ptr = FixedEncodeUint16(buf, buf+sizeof(buf), value);
    EXPECT_EQ(ptr - buf, sizeof(uint16_t));

    uint16_t value2 = 0;
    const uint8_t* ret = FixedDecodeUint16(buf, buf+sizeof(buf), value2);
    EXPECT_EQ(ret, ptr);
    EXPECT_EQ(value, value2);
}

}
}
}