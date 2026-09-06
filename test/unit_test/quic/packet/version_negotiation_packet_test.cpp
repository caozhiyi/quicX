#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/packet/version_negotiation_packet.h"

namespace quicx {
namespace quic {
namespace {

TEST(VersionNegotiationPacketTest, codec) {
    VersionNegotiationPacket packet;
    std::vector<uint32_t> versions = {1, 2, 3, 4};
    packet.SetSupportVersion(versions);
    packet.AddSupportVersion(5);

    // See header_flag_test for why the buffer starts empty (append
    // semantics required by packet coalescing).
    static const uint8_t s_buf_len = 128;
    std::shared_ptr<common::SingleBlockBuffer> buffer =
        std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(s_buf_len));

    EXPECT_TRUE(packet.Encode(buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(buffer));

    VersionNegotiationPacket new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.DecodeWithoutCrypto(buffer));

    auto new_versions = new_packet.GetSupportVersion();
    EXPECT_EQ(new_versions.size(), 5);
    for (uint32_t i = 0; i < new_versions.size(); i++) {
        EXPECT_EQ(new_versions[i], i + 1);
    }
}

}  // namespace
}  // namespace quic
}  // namespace quicx