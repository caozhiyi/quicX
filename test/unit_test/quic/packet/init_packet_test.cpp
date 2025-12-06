#include <gtest/gtest.h>

#include "common/buffer/single_block_buffer.h"
#include "common/buffer/standalone_buffer_chunk.h"

#include "quic/packet/init_packet.h"
#include "quic/packet/init_packet.h"

#include "test/unit_test/quic/packet/common_test_frame.h"

namespace quicx {
namespace quic {
namespace {

TEST(init_packet_utest, codec) {
    auto frame = PacketTest::GetTestFrame();

    // Create empty buffer for encoding frame
    std::shared_ptr<common::SingleBlockBuffer> frame_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(kBufLength));
    EXPECT_TRUE(frame->Encode(frame_buffer));

    InitPacket packet;
    common::SharedBufferSpan payload(frame_buffer->GetSharedReadableSpan());
    packet.SetPayload(payload);
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);

    // Create empty buffer for encoding packet
    std::shared_ptr<common::SingleBlockBuffer> packet_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(kBufLength));
    EXPECT_TRUE(packet.Encode(packet_buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(packet_buffer));

    InitPacket new_packet(flag.GetFlag());
    EXPECT_TRUE(new_packet.DecodeWithoutCrypto(packet_buffer));
    EXPECT_TRUE(new_packet.DecodeWithCrypto(nullptr));

    EXPECT_EQ(new_packet.GetPacketNumber(), 10);
    EXPECT_EQ(new_packet.GetHeader()->GetPacketNumberLength(), 2);

    auto frames = new_packet.GetFrames();
    EXPECT_EQ(frames.size(), 1) << "Expected 1 frame but got " << frames.size();
    
    if (frames.size() == 1 && frames[0] != nullptr) {
        EXPECT_TRUE(PacketTest::CheckTestFrame(frames[0]));
    }
}

TEST(init_packet_utest, crypto_codec) {
    auto frame = PacketTest::GetTestFrame();

    // Create empty buffer for encoding frame
    std::shared_ptr<common::SingleBlockBuffer> frame_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(kBufLength));
    EXPECT_TRUE(frame->Encode(frame_buffer));

    InitPacket packet;
    common::SharedBufferSpan payload(frame_buffer->GetSharedReadableSpan());
    packet.SetPayload(payload);
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);
    packet.SetCryptographer(PacketTest::Instance().GetTestClientCryptographer());

    // Create empty buffer for encoding packet
    std::shared_ptr<common::SingleBlockBuffer> packet_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(kBufLength));
    EXPECT_TRUE(packet.Encode(packet_buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(packet_buffer));

    InitPacket new_packet(flag.GetFlag());
    new_packet.SetCryptographer(PacketTest::Instance().GetTestServerCryptographer());
    EXPECT_TRUE(new_packet.DecodeWithoutCrypto(packet_buffer));

    // Create empty buffer for decrypting packet
    std::shared_ptr<common::SingleBlockBuffer> plaintext_buffer = std::make_shared<common::SingleBlockBuffer>(std::make_shared<common::StandaloneBufferChunk>(kBufLength));
    EXPECT_TRUE(new_packet.DecodeWithCrypto(plaintext_buffer));

    EXPECT_EQ(new_packet.GetPacketNumber(), 10);
    EXPECT_EQ(new_packet.GetHeader()->GetPacketNumberLength(), 2);

    auto frames = new_packet.GetFrames();
    EXPECT_EQ(frames.size(), 1) << "Expected 1 frame but got " << frames.size();
    
    if (frames.size() == 1 && frames[0] != nullptr) {
        EXPECT_TRUE(PacketTest::CheckTestFrame(frames[0]));
    }
}

}
}
}