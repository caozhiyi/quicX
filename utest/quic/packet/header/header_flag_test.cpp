#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/packet/header/header_flag.h"

namespace quicx {
namespace {


TEST(header_flag_utest, rtt1_create) {
    HeaderFlag flag(PHT_SHORT_HEADER);
    flag.SetPacketNumberLength(2);
    flag.GetShortHeaderFlag().SetKeyPhase(1);
    flag.GetShortHeaderFlag().SetReservedBits(1);
    flag.GetShortHeaderFlag().SetSpinBit(1);

    EXPECT_EQ(flag.GetShortHeaderFlag().GetSpinBit(), 1);
    EXPECT_EQ(flag.GetShortHeaderFlag().GetReservedBits(), 1);
    EXPECT_EQ(flag.GetShortHeaderFlag().GetKeyPhase(), 1);
    EXPECT_EQ(flag.GetPacketNumberLength(), 2);
    EXPECT_EQ(flag.GetHeaderType(), PHT_SHORT_HEADER);
    EXPECT_EQ(flag.GetPacketType(), PT_1RTT);
    EXPECT_EQ(flag.GetFixBit(), 1);
}

TEST(header_flag_utest, init_create) {
    HeaderFlag flag(PHT_LONG_HEADER);
    flag.SetPacketNumberLength(2);
    flag.GetLongHeaderFlag().SetPacketType(PT_INITIAL);
    flag.GetLongHeaderFlag().SetReservedBits(1);

    EXPECT_EQ(flag.GetLongHeaderFlag().GetReservedBits(), 1);
    EXPECT_EQ(flag.GetPacketNumberLength(), 2);
    EXPECT_EQ(flag.GetHeaderType(), PHT_LONG_HEADER);
    EXPECT_EQ(flag.GetPacketType(), PT_INITIAL);
    EXPECT_EQ(flag.GetFixBit(), 1);
}

TEST(header_flag_utest, rtt1_codec) {
    HeaderFlag flag(PHT_SHORT_HEADER);
    flag.SetPacketNumberLength(2);
    flag.GetShortHeaderFlag().SetKeyPhase(1);
    flag.GetShortHeaderFlag().SetReservedBits(1);
    flag.GetShortHeaderFlag().SetSpinBit(1);

    static const uint8_t __buf_len = 2;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> buffer = std::make_shared<Buffer>(buf, buf + __buf_len);

    EXPECT_TRUE(flag.EncodeFlag(buffer));
    EXPECT_EQ(buffer->GetDataLength(), 1);

    HeaderFlag new_flag;
    EXPECT_TRUE(new_flag.DecodeFlag(buffer));

    EXPECT_EQ(new_flag.GetShortHeaderFlag().GetSpinBit(), 1);
    EXPECT_EQ(new_flag.GetShortHeaderFlag().GetReservedBits(), 1);
    EXPECT_EQ(new_flag.GetShortHeaderFlag().GetKeyPhase(), 1);
    EXPECT_EQ(new_flag.GetPacketNumberLength(), 2);
    EXPECT_EQ(new_flag.GetHeaderType(), PHT_SHORT_HEADER);
    EXPECT_EQ(new_flag.GetPacketType(), PT_1RTT);
    EXPECT_EQ(new_flag.GetFixBit(), 1);
}

TEST(header_flag_utest, init_codec) {
    HeaderFlag flag(PHT_LONG_HEADER);
    flag.SetPacketNumberLength(2);
    flag.GetLongHeaderFlag().SetPacketType(PT_INITIAL);
    flag.GetLongHeaderFlag().SetReservedBits(1);

    static const uint8_t __buf_len = 2;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> buffer = std::make_shared<Buffer>(buf, buf + __buf_len);

    EXPECT_TRUE(flag.EncodeFlag(buffer));
    EXPECT_EQ(buffer->GetDataLength(), 1);

    HeaderFlag new_flag;
    EXPECT_TRUE(new_flag.DecodeFlag(buffer));

    EXPECT_EQ(new_flag.GetLongHeaderFlag().GetReservedBits(), 1);
    EXPECT_EQ(new_flag.GetPacketNumberLength(), 2);
    EXPECT_EQ(new_flag.GetHeaderType(), PHT_LONG_HEADER);
    EXPECT_EQ(new_flag.GetPacketType(), PT_INITIAL);
    EXPECT_EQ(new_flag.GetFixBit(), 1);
}

}
}