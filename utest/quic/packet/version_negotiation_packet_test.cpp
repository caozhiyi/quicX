#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/packet/version_negotiation_packet.h"

namespace quicx {
namespace {

TEST(version_negotiation_packet_utest, codec) {
    VersionNegotiationPacket packet;
    std::vector<uint32_t> versions = {1,2,3,4};
    packet.SetSupportVersion(versions);
    packet.AddSupportVersion(5);

    static const uint8_t __buf_len = 128;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> buffer = std::make_shared<Buffer>(buf, buf + __buf_len);

    EXPECT_TRUE(packet.Encode(buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(buffer));

    VersionNegotiationPacket new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.DecodeBeforeDecrypt(buffer));


    auto new_versions = new_packet.GetSupportVersion();
    EXPECT_EQ(new_versions.size(), 5);
    for (uint32_t i = 0; i < new_versions.size(); i++) {
        EXPECT_EQ(new_versions[i], i+1);
    }
}

}
}