#include <gtest/gtest.h>

#include "common/decode/decode.h"

namespace quicx {
namespace common {
namespace {

TEST(DecodeTest, EncodeVarint64_1) {
    uint64_t value = 1 << 5;
    uint8_t buf[5];
    uint8_t* ptr1 = EncodeVarint(buf, buf + sizeof(buf), value);
    EXPECT_EQ(ptr1 - buf, 1);

    uint64_t value2 = 0;
    const uint8_t* ptr2 = DecodeVarint(buf, buf + sizeof(buf), value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(DecodeTest, EncodeVarint64_2) {
    uint64_t value = 1 << 13;
    uint8_t buf[5];
    uint8_t* ptr1 = EncodeVarint(buf, buf + sizeof(buf), value);
    EXPECT_EQ(ptr1 - buf, 2);

    uint64_t value2 = 0;
    const uint8_t* ptr2 = DecodeVarint(buf, buf + sizeof(buf), value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(DecodeTest, EncodeVarint64_3) {
    uint64_t value = 1 << 29;
    uint8_t buf[5];
    uint8_t* ptr1 = EncodeVarint(buf, buf + sizeof(buf), value);
    EXPECT_EQ(ptr1 - buf, 4);

    uint64_t value2 = 0;
    const uint8_t* ptr2 = DecodeVarint(buf, buf + sizeof(buf), value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(DecodeTest, EncodeVarint64_4) {
    uint64_t value = (uint64_t)1 << 60;
    uint8_t buf[10];
    uint8_t* ptr1 = EncodeVarint(buf, buf + sizeof(buf), value);
    EXPECT_EQ(ptr1 - buf, 8);

    uint64_t value2 = 0;
    const uint8_t* ptr2 = DecodeVarint(buf, buf + sizeof(buf), value2);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value, value2);
}

TEST(DecodeTest, DecodeVarint32_MaxValue) {
    // Exactly UINT32_MAX must decode fine through the 32-bit overload.
    uint64_t value = UINT32_MAX;
    uint8_t buf[10];
    uint8_t* ptr1 = EncodeVarint(buf, buf + sizeof(buf), value);
    ASSERT_NE(ptr1, nullptr);

    uint32_t value32 = 0;
    uint8_t* ptr2 = DecodeVarint(buf, buf + sizeof(buf), value32);
    EXPECT_EQ(ptr1, ptr2);
    EXPECT_EQ(value32, UINT32_MAX);
}

TEST(DecodeTest, DecodeVarint32_RejectOverflow) {
    // A well-formed 8-byte varint whose value exceeds 32 bits must be
    // rejected (nullptr) instead of being silently truncated.
    const uint64_t kOverflowValues[] = {
        (uint64_t)UINT32_MAX + 1,
        (uint64_t)1 << 40,
        (uint64_t)1 << 60,
    };
    for (uint64_t value : kOverflowValues) {
        uint8_t buf[10];
        uint8_t* ptr1 = EncodeVarint(buf, buf + sizeof(buf), value);
        ASSERT_NE(ptr1, nullptr);

        uint32_t value32 = 0;
        uint8_t* ptr2 = DecodeVarint(buf, buf + sizeof(buf), value32);
        EXPECT_EQ(ptr2, nullptr) << "value=" << value;

        // The 64-bit overload must still decode the same bytes fine.
        uint64_t value64 = 0;
        uint8_t* ptr3 = DecodeVarint(buf, buf + sizeof(buf), value64);
        EXPECT_EQ(ptr3, ptr1);
        EXPECT_EQ(value64, value);
    }
}

}  // namespace
}  // namespace common
}  // namespace quicx