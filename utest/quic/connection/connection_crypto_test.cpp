#include <gtest/gtest.h>
#include "common/log/log.h"
#include "quic/connection/type.h"
#include "quic/packet/init_packet.h"
#include "quic/connection/client_connection.h"
#include "quic/connection/connection_id_generator.h"

namespace quicx {
namespace {

bool Decrypt(std::shared_ptr<ICryptographer>& cryptographer, std::shared_ptr<IPacket> packet,
    std::shared_ptr<IBufferWrite> out_plaintext) {
    auto header = packet->GetHeader();
    // get sample
    BufferSpan header_span = header->GetHeaderSrcData();
    LOG_DEBUG("decrypt header start:%p", header_span.GetStart());
    LOG_DEBUG("decrypt header end:%p", header_span.GetEnd());

    uint32_t packet_offset = packet->GetPacketNumOffset();
    BufferSpan sample = BufferSpan(header_span.GetEnd() + packet->GetPacketNumOffset() + 4,
        header_span.GetEnd() + packet->GetPacketNumOffset() + 4 + __header_protect_sample_length);
    // decrypto header
    uint64_t packet_num = 0;
    uint32_t packet_num_len = 0;
    LOG_DEBUG("decrypt sample start:%p", sample.GetStart());
    LOG_DEBUG("decrypt sample end:%p", sample.GetEnd());
    LOG_DEBUG("decrypt packet number start:%p", header_span.GetEnd() + packet->GetPacketNumOffset());
    if(!cryptographer->DecryptHeader(header_span, sample, header_span.GetLength() + packet_offset, header->GetHeaderType() == PHT_SHORT_HEADER,
        packet_num, packet_num_len)) {
        LOG_ERROR("decrypt header failed.");
        return false;
    }

    LOG_DEBUG("decrypt packet num len:%d", header_span.GetLength() + packet_offset);
    // decrypto packet
    auto payload = BufferSpan(header_span.GetEnd() + packet->GetPacketNumOffset() + packet_num_len, packet->GetSrcBuffer().GetEnd()); 
    LOG_DEBUG("decrypt payload start:%p", payload.GetStart());
    LOG_DEBUG("decrypt payload end:%p", payload.GetEnd());
    if(!cryptographer->DecryptPacket(packet_num, header->GetHeaderSrcData(), payload, out_plaintext)) {
        LOG_ERROR("decrypt packet failed.");
        return false;
    }

    return true;
}

bool Encrypt(std::shared_ptr<ICryptographer>& cryptographer, std::shared_ptr<IPacket> packet, 
    std::shared_ptr<IBuffer> out_ciphertext) {
    auto header = packet->GetHeader();

    out_ciphertext->Write(header->GetHeaderSrcData().GetStart(), header->GetHeaderSrcData().GetLength());
    auto header_span = out_ciphertext->GetReadSpan();

    LOG_DEBUG("encrypt packet num len:%d", header_span.GetLength() + packet->GetPacketNumOffset());
    out_ciphertext->Write(packet->GetSrcBuffer().GetStart(), packet->GetPacketNumOffset() + header->GetPacketNumberLength());
    auto test_span = out_ciphertext->GetReadSpan();

    LOG_DEBUG("encrypt payload start:%p", test_span.GetEnd());
    auto payload = BufferSpan(packet->GetSrcBuffer().GetStart() + packet->GetPacketNumOffset() + header->GetPacketNumberLength(), packet->GetSrcBuffer().GetEnd());
    // packet protection
    if(!cryptographer->EncryptPacket(packet->GetPacketNumber(), header->GetHeaderSrcData(), payload, out_ciphertext)) {
        LOG_ERROR("encrypt packet failed.");
        return false;
    }
    test_span = out_ciphertext->GetReadSpan();
    LOG_DEBUG("encrypt payload end:%p", test_span.GetEnd());

    LOG_DEBUG("encrypt header start:%p", header_span.GetStart());
    LOG_DEBUG("encrypt header end:%p", header_span.GetEnd());

    // header protection
    uint32_t packet_offset = packet->GetPacketNumOffset();
    BufferSpan sample = BufferSpan(header_span.GetEnd() + packet->GetPacketNumOffset() + 4,
        header_span.GetEnd() + packet->GetPacketNumOffset() + 4 + __header_protect_sample_length);

    LOG_DEBUG("encrypt sample start:%p", sample.GetStart());
    LOG_DEBUG("encrypt sample end:%p", sample.GetEnd());

    LOG_DEBUG("encrypt packet number start:%p", header_span.GetEnd() + packet->GetPacketNumOffset());
    if(!cryptographer->EncryptHeader(header_span, sample, header_span.GetLength() + packet->GetPacketNumOffset(), header->GetPacketNumberLength(),
        header->GetHeaderType() == PHT_SHORT_HEADER)) {
        LOG_ERROR("decrypt header failed.");
        return false;
    }

    return true;
}

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