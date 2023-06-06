#include <gtest/gtest.h>
#include "common/log/log.h"
#include "quic/connection/type.h"
#include "quic/packet/init_packet.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace {

/*
TEST(connnection_crypto_utest, generator) {
    uint8_t dcid[__max_cid_length] = {0};
    ConnectionIDGenerator::Instance().Generator(dcid, __max_cid_length);

    auto cli_cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
    EXPECT_TRUE(cli_cryptographer->InstallInitSecret(dcid, __max_cid_length,
            __initial_slat, sizeof(__initial_slat), false));

    auto ser_cryptographer = MakeCryptographer(CI_TLS1_CK_AES_128_GCM_SHA256);
    EXPECT_TRUE(ser_cryptographer->InstallInitSecret(dcid, __max_cid_length,
            __initial_slat, sizeof(__initial_slat), true));

    uint8_t data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30};
    auto packet1 = std::make_shared<InitPacket>();
    BufferSpan payload(data, sizeof(data));
    packet1->SetPayload(payload);
    packet1->SetPacketNumber(10);
    ((LongHeader*)packet1->GetHeader())->SetPacketNumberLength(2);
    ((LongHeader*)packet1->GetHeader())->SetSourceConnectionId(data, 10);
    ((LongHeader*)packet1->GetHeader())->SetDestinationConnectionId(data, 10);
    ((LongHeader*)packet1->GetHeader())->SetVersion(1);

    static const uint32_t __buf_len = 1024;
    uint8_t src_buf[__buf_len] = {0};
    std::shared_ptr<IBuffer> src_buffer = std::make_shared<Buffer>(src_buf, src_buf + __buf_len);

    EXPECT_TRUE(packet1->Encode(src_buffer));

    uint8_t cipher_buf[__buf_len];
    std::shared_ptr<IBuffer> cipher_buffer = std::make_shared<Buffer>(cipher_buf, cipher_buf + __buf_len);

    // encrypt 
    EXPECT_TRUE(Encrypt(cli_cryptographer, packet1, cipher_buffer));

    HeaderFlag flag;
    EXPECT_TRUE(flag.DecodeFlag(cipher_buffer));
    auto packet2 = std::make_shared<InitPacket>(flag.GetFlag());

    EXPECT_TRUE(packet2->DecodeBeforeDecrypt(cipher_buffer));
    // decrypt
    uint8_t dest_buf[__buf_len];
    std::shared_ptr<IBuffer> dest_buffer = std::make_shared<Buffer>(dest_buf, dest_buf + __buf_len);
    EXPECT_TRUE(Decrypt(ser_cryptographer, packet2, dest_buffer));
    EXPECT_FALSE(packet2->DecodeAfterDecrypt(dest_buffer));

    auto new_payload = packet2->GetPayload();
    EXPECT_EQ(new_payload.GetLength(), sizeof(data));
    for (uint32_t i = 0; i < sizeof(data); i++) {
        EXPECT_EQ(data[i], *(new_payload.GetStart() + i));
    }
}
*/

}
}