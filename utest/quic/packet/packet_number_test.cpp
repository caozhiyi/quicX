#include <gtest/gtest.h>

#include "quic/packet/packet_number.h"

namespace quicx {
namespace {

TEST(packet_number_utest, codec) {
    PacketNumber packet_number;

    for (size_t i = 0; i < 1000; i++) {
        packet_number.NextPakcetNumber(PNS_APPLICATION);
    }

    uint64_t pn = packet_number.NextPakcetNumber(PNS_APPLICATION);
    EXPECT_EQ(pn, 1000);

    uint8_t buf[4] = {0};
    PacketNumber::Encode(buf, 2, pn);

    uint64_t new_pn = 0;
    PacketNumber::Decode(buf, 2, new_pn);

    EXPECT_EQ(pn, new_pn);
    
    uint64_t except_pn = PacketNumber::Decode(999, new_pn, 8);
    EXPECT_EQ(except_pn, new_pn);
}

}
}