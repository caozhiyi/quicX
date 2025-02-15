#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/packet/retry_packet.h"

namespace quicx {
namespace quic {
namespace {

TEST(retry_packet_utest, codec) {
    uint8_t tag[kRetryIntegrityTagLength];
    for (size_t i = 0; i < kRetryIntegrityTagLength; i++) {
        tag[i] = i;
    }

    RetryPacket packet;
    packet.SetRetryIntegrityTag(tag);
    packet.SetRetryToken(common::BufferSpan(tag, 64));

    static const uint32_t __buf_len = 256;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<common::IBuffer> buffer = std::make_shared<common::Buffer>(buf, __buf_len);

    EXPECT_TRUE(packet.Encode(buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(buffer));

    RetryPacket new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.DecodeWithoutCrypto(buffer));

    auto new_tag = new_packet.GetRetryIntegrityTag();
    for (uint32_t i = 0; i < kRetryIntegrityTagLength; i++) {
        EXPECT_EQ(*(new_tag + i), i);
    }

    auto new_token = new_packet.GetRetryToken();
    for (uint32_t i = 0; i < 64; i++) {
        EXPECT_EQ(*(new_token.GetStart() + i), i);
    }
}

}
}
}