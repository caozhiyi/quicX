#include <gtest/gtest.h>
#include "common/decode/decode.h"

TEST(decode_utest, EncodeVarint64_1) {
    uint64_t value = 1 << 5;
    uint8_t buf[5];
    uint8_t* ptr1 = quicx::EncodeVarint(buf, value);
    EXPECT_EQ(ptr1 - buf, 1);

    uint64_t value2 = 0;
    uint8_t* ptr2 = quicx::DecodeVarint(buf, buf+5, value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_2) {
    uint64_t value = 1 << 13;
    uint8_t buf[5];
    uint8_t* ptr1 = quicx::EncodeVarint(buf, value);
    EXPECT_EQ(ptr1 - buf, 2);

    uint64_t value2 = 0;
    uint8_t* ptr2 = quicx::DecodeVarint(buf, buf+5, value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_3) {
    uint64_t value = 1 << 29;
    uint8_t buf[5];
    uint8_t* ptr1 = quicx::EncodeVarint(buf, value);
    EXPECT_EQ(ptr1 - buf, 4);

    uint64_t value2 = 0;
    uint8_t* ptr2 = quicx::DecodeVarint(buf, buf+5, value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(decode_utest, EncodeVarint64_4) {
    uint64_t value = (uint64_t)1 << 60;
    uint8_t buf[10];
    uint8_t* ptr1 = quicx::EncodeVarint(buf, value);
    EXPECT_EQ(ptr1 - buf, 8);

    uint64_t value2 = 0;
    uint8_t* ptr2 = quicx::DecodeVarint(buf, buf+5, value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

// TEST(decode_utest, decode_bytes_copy) {
//     const uint8_t* str = "12345678910";
//     uint8_t buf[20] = {};
//     uint8_t* ptr1 = quicx::EncodeBytes(buf, buf + 20, str, strlen((const char*)str));
//     EXPECT_EQ(ptr1 - buf, strlen((const char*)str));

//     uint8_t buf2[20] = {};
//     uint8_t* bufptr = buf2;
//     uint8_t* ptr2 = quicx::DecodeBytesCopy(buf, buf + 20, bufptr, strlen((const char*)str));
//     EXPECT_EQ(strcmp((const char*)str, (const char*)bufptr), 0);
// }

// TEST(decode_utest, decode_bytes_not_copy) {
//     const uint8_t* str = "12345678910";
//     uint8_t buf[20] = {};
//     uint8_t* ptr1 = quicx::EncodeBytes(buf, buf + 20, str, strlen((const char*)str));
//     EXPECT_EQ(ptr1 - buf, strlen((const char*)str));

//     uint8_t* bufptr;
//     uint8_t* ptr2 = quicx::DecodeBytesNoCopy(buf, buf + 20, bufptr, strlen((const char*)str));
//     EXPECT_EQ(strcmp((const char*)str, (const char*)bufptr), 0);
// }