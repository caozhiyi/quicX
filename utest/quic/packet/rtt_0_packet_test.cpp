#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/connection/type.h"
#include "quic/packet/rtt_0_packet.h"
#include "utest/quic/packet/common_test_frame.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace {

TEST(rtt_0_packet_utest, codec) {
    auto frame = PacketTest::GetTestFrame();

    uint8_t frame_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> frame_buffer = std::make_shared<Buffer>(frame_buf, __buf_len);
    EXPECT_TRUE(frame->Encode(frame_buffer));

    Rtt0Packet packet;
    BufferSpan payload(frame_buffer->GetReadSpan());
    packet.SetPayload(payload);
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);

    static const uint8_t __buf_len = 128;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> packet_buffer = std::make_shared<Buffer>(buf, __buf_len);

    EXPECT_TRUE(packet.Encode(packet_buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(packet_buffer));

    Rtt0Packet new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.Decode(packet_buffer));
    EXPECT_TRUE(new_packet.Decode(nullptr, nullptr));

    EXPECT_EQ(new_packet.GetPacketNumber(), 10);
    EXPECT_EQ(new_packet.GetHeader()->GetPacketNumberLength(), 2);

    auto frames = new_packet.GetFrames();
    EXPECT_EQ(frames.size(), 1);

    EXPECT_TRUE(PacketTest::CheckTestFrame(frames[0]));
}

TEST(rtt_0_packet_utest, crypto_codec) {
    auto frame = PacketTest::GetTestFrame();

    uint8_t frame_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> frame_buffer = std::make_shared<Buffer>(frame_buf, __buf_len);
    EXPECT_TRUE(frame->Encode(frame_buffer));

    Rtt0Packet packet;
    BufferSpan payload(frame_buffer->GetReadSpan());
    packet.SetPayload(payload);
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);

    static const uint8_t __buf_len = 128;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> packet_buffer = std::make_shared<Buffer>(buf, __buf_len);

    EXPECT_TRUE(packet.Encode(packet_buffer, PacketTest::Instance().GetTestClientCryptographer()));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(packet_buffer));

    Rtt0Packet new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.Decode(packet_buffer));
    std::shared_ptr<ICryptographer> crypto_grapher;

    uint8_t plaintext_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> plaintext_buffer = std::make_shared<Buffer>(plaintext_buf, __buf_len);
    EXPECT_TRUE(new_packet.Decode(plaintext_buffer, PacketTest::Instance().GetTestServerCryptographer()));

    EXPECT_EQ(new_packet.GetPacketNumber(), 10);
    EXPECT_EQ(new_packet.GetHeader()->GetPacketNumberLength(), 2);

    auto frames = new_packet.GetFrames();
    EXPECT_EQ(frames.size(), 1);

    EXPECT_TRUE(PacketTest::CheckTestFrame(frames[0]));
}

}
}