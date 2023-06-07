#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/frame/crypto_frame.h"
#include "quic/packet/rtt_1_packet.h"
#include "utest/quic/packet/common_test_frame.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace {

TEST(rtt_1_packet_utest, codec) {
    auto frame = PacketTest::GetTestFrame();

    uint8_t frame_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> frame_buffer = std::make_shared<Buffer>(frame_buf, __buf_len);
    EXPECT_TRUE(frame->Encode(frame_buffer));

    Rtt1Packet packet;
    packet.SetPayload(frame_buffer->GetReadSpan());
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);

    
    uint8_t packet_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> packet_buffer = std::make_shared<Buffer>(packet_buf, __buf_len);
    EXPECT_TRUE(packet.Encode(packet_buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(packet_buffer));

    Rtt1Packet new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.Decode(packet_buffer));
    EXPECT_TRUE(new_packet.Decode(nullptr, nullptr));

    auto frames = new_packet.GetFrames();
    EXPECT_EQ(frames.size(), 1);

    EXPECT_TRUE(PacketTest::CheckTestFrame(frames[0]));
}


TEST(rtt_1_packet_utest, crypto_codec) {
    auto frame = PacketTest::GetTestFrame();

    uint8_t frame_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> frame_buffer = std::make_shared<Buffer>(frame_buf, frame_buf + __buf_len);
    EXPECT_TRUE(frame->Encode(frame_buffer));

    Rtt1Packet packet;
    packet.SetPayload(frame_buffer->GetReadSpan());
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);

    uint8_t packet_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> packet_buffer = std::make_shared<Buffer>(packet_buf, packet_buf + __buf_len);
    EXPECT_TRUE(packet.Encode(packet_buffer, PacketTest::Instance().GetTestClientCryptographer()));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(packet_buffer));

    Rtt1Packet new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.Decode(packet_buffer));

    uint8_t plaintext_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> plaintext_buffer = std::make_shared<Buffer>(plaintext_buf, __buf_len);
    EXPECT_TRUE(new_packet.Decode(PacketTest::Instance().GetTestServerCryptographer(), plaintext_buffer));

    auto frames = new_packet.GetFrames();
    EXPECT_EQ(frames.size(), 1);

    EXPECT_TRUE(PacketTest::CheckTestFrame(frames[0]));
}

}
}