#include <gtest/gtest.h>

#include "common/buffer/buffer.h"
#include "quic/connection/type.h"
#include "quic/packet/init_packet.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace {

TEST(init_packet_utest, codec) {
    InitPacket packet;
    uint8_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
    BufferSpan payload(data, sizeof(data));
    packet.SetPayload(payload);
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);

    static const uint8_t __buf_len = 128;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> buffer = std::make_shared<Buffer>(buf, buf + __buf_len);

    EXPECT_TRUE(packet.Encode(buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(buffer));

    InitPacket new_packet(flag.GetFlag());
    EXPECT_FALSE(new_packet.DecodeBeforeDecrypt(buffer));

    EXPECT_EQ(new_packet.GetPacketNumber(), 10);
    EXPECT_EQ(new_packet.GetHeader()->GetPacketNumberLength(), 2);

    auto new_payload = new_packet.GetPayload();
    EXPECT_EQ(new_payload.GetLength(), sizeof(data));
    for (uint32_t i = 0; i < sizeof(data); i++) {
        EXPECT_EQ(data[i], *(new_payload.GetStart() + i));
    }
}

TEST(init_packet_utest, crypto_codec) {
    uint8_t dcid[__max_cid_length] = {0};
    ConnectionIDGenerator::Instance().Generator(dcid, __max_cid_length);

    auto cli_cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
    EXPECT_TRUE(cli_cryptographer->InstallInitSecret(dcid, __max_cid_length,
            __initial_slat, sizeof(__initial_slat), false));

    auto ser_cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
    EXPECT_TRUE(ser_cryptographer->InstallInitSecret(dcid, __max_cid_length,
            __initial_slat, sizeof(__initial_slat), true));

    InitPacket packet;
    uint8_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
    BufferSpan payload(data, sizeof(data));
    packet.SetPayload(payload);
    packet.SetPacketNumber(10);
    packet.GetHeader()->SetPacketNumberLength(2);
    packet.SetCryptographer(cli_cryptographer);

    static const uint8_t __buf_len = 128;
    uint8_t buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> buffer = std::make_shared<Buffer>(buf, buf + __buf_len);

    EXPECT_TRUE(packet.Encode(buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(buffer));

    InitPacket new_packet(flag.GetFlag());
    new_packet.SetCryptographer(ser_cryptographer);
    EXPECT_FALSE(new_packet.DecodeBeforeDecrypt(buffer));

    EXPECT_EQ(new_packet.GetPacketNumber(), 10);
    EXPECT_EQ(new_packet.GetHeader()->GetPacketNumberLength(), 2);

    auto new_payload = new_packet.GetPayload();
    EXPECT_EQ(new_payload.GetLength(), sizeof(data));
    for (uint32_t i = 0; i < sizeof(data); i++) {
        EXPECT_EQ(data[i], *(new_payload.GetStart() + i));
    }
}

}
}