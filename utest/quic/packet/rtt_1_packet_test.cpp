#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/packet/rtt_1_packet.h"

namespace quicx {
namespace {

TEST(rtt_1_packet_utest, codec) {
    Rtt1Packet packet;
    uint8_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
    BufferSpan payload(data, sizeof(data));
    packet.SetPayload(payload);

    static const uint8_t __buf_len = 128;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> buffer = std::make_shared<Buffer>(buf, buf + __buf_len);

    EXPECT_TRUE(packet.Encode(buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(buffer));

    Rtt1Packet new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.DecodeBeforeDecrypt(buffer));

    auto alloter = MakeBlockMemoryPoolPtr(1024, 4);
    auto payload_buffer = std::make_shared<Buffer>(alloter);
    payload_buffer->Write(new_packet.GetSrcBuffer().GetStart(), new_packet.GetSrcBuffer().GetLength());
    EXPECT_FALSE(new_packet.DecodeAfterDecrypt(payload_buffer));

    auto new_payload = new_packet.GetPayload();
    EXPECT_EQ(new_payload.GetLength(), sizeof(data));
    for (uint32_t i = 0; i < sizeof(data); i++) {
        EXPECT_EQ(data[i], *(new_payload.GetStart() + i));
    }
}

}
}