#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/frame/crypto_frame.h"
#include "quic/packet/rtt_1_packet.h"
#include "utest/quic/packet/common_test_frame.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace quic {
namespace {

TEST(rtt_1_packet_utest, codec) {
    auto frame = PacketTest::GetTestFrame();

    uint8_t frame_buf[kBufLength] = {0};
    std::shared_ptr<common::IBuffer> frame_buffer = std::make_shared<common::Buffer>(frame_buf, kBufLength);
    EXPECT_TRUE(frame->Encode(frame_buffer));

    Rtt1Packet packet;
    packet.SetPayload(frame_buffer->GetReadSpan());
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);

    
    uint8_t packet_buf[kBufLength] = {0};
    std::shared_ptr<common::IBuffer> packet_buffer = std::make_shared<common::Buffer>(packet_buf, kBufLength);
    EXPECT_TRUE(packet.Encode(packet_buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(packet_buffer));

    Rtt1Packet new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.DecodeWithoutCrypto(packet_buffer));
    EXPECT_TRUE(new_packet.DecodeWithCrypto(nullptr));

    auto frames = new_packet.GetFrames();
    EXPECT_EQ(frames.size(), 1);

    EXPECT_TRUE(PacketTest::CheckTestFrame(frames[0]));
}


TEST(rtt_1_packet_utest, crypto_codec) {
    auto frame = PacketTest::GetTestFrame();

    uint8_t frame_buf[kBufLength] = {0};
    std::shared_ptr<common::IBuffer> frame_buffer = std::make_shared<common::Buffer>(frame_buf, frame_buf + kBufLength);
    EXPECT_TRUE(frame->Encode(frame_buffer));

    Rtt1Packet packet;
    packet.SetPayload(frame_buffer->GetReadSpan());
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);
    packet.SetCryptographer(PacketTest::Instance().GetTestClientCryptographer());

    uint8_t packet_buf[kBufLength] = {0};
    std::shared_ptr<common::IBuffer> packet_buffer = std::make_shared<common::Buffer>(packet_buf, packet_buf + kBufLength);
    EXPECT_TRUE(packet.Encode(packet_buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(packet_buffer));

    Rtt1Packet new_packet(flag.GetFlag());
    new_packet.SetCryptographer(PacketTest::Instance().GetTestServerCryptographer());
    EXPECT_TRUE(new_packet.DecodeWithoutCrypto(packet_buffer));

    uint8_t plaintext_buf[kBufLength] = {0};
    std::shared_ptr<common::IBuffer> plaintext_buffer = std::make_shared<common::Buffer>(plaintext_buf, kBufLength);
    EXPECT_TRUE(new_packet.DecodeWithCrypto(plaintext_buffer));

    auto frames = new_packet.GetFrames();
    EXPECT_EQ(frames.size(), 1);

    EXPECT_TRUE(PacketTest::CheckTestFrame(frames[0]));
}

}
}
}