#include <cstring>
#include <gtest/gtest.h>
#include "common/buffer/buffer.h"
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_view.h"
#include "quic/packet/header/short_header.h"
#include "common/buffer/buffer_write_view.h"
#include "utest/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {
namespace quic {

bool DecryptPacketTest(std::shared_ptr<ICryptographer> encrypter, std::shared_ptr<ICryptographer> decrypter) {
    std::shared_ptr<common::BlockMemoryPool> pool = std::make_shared<common::BlockMemoryPool>(2048, 5);

    static const uint32_t __plaintext_length = 1024;
    
    // make test plaintext
    std::shared_ptr<common::Buffer> plaintext = std::make_shared<common::Buffer>(pool);
    auto plaintext_span = plaintext->GetWriteSpan();
    uint8_t* plaintext_pos = plaintext_span.GetStart();
    for (uint16_t i = 0; i < __plaintext_length; i++) {
        *(plaintext_pos + i) = i;
    }
    plaintext->MoveWritePt(__plaintext_length);

    uint64_t pkt_num = 102154;
    std::shared_ptr<common::Buffer> out_ciphertext = std::make_shared<common::Buffer>(pool);
    common::BufferSpan associated_data_span = common::BufferSpan((uint8_t*)__associated_data, (uint8_t*)__associated_data + sizeof(__associated_data));
    common::BufferSpan plaintext_span2 = plaintext->GetReadSpan();
    if (!encrypter->EncryptPacket(pkt_num, associated_data_span, plaintext_span2, out_ciphertext)) {
        ADD_FAILURE() << encrypter->GetName() << " EncryptPacket failed";
        return false;
    }
    
    std::shared_ptr<common::Buffer> out_plaintext = std::make_shared<common::Buffer>(pool);
    common::BufferSpan associated_data_span1 = common::BufferSpan((uint8_t*)__associated_data,  (uint8_t*)__associated_data + sizeof(__associated_data));
    common::BufferSpan plaintext_span1 = out_ciphertext->GetReadSpan();
    if (!decrypter->DecryptPacket(pkt_num, associated_data_span1, plaintext_span1, out_plaintext)) {
        ADD_FAILURE() << decrypter->GetName() << " DecryptPacket failed";
        return false;
    }

    if (plaintext->GetDataLength() != out_plaintext->GetDataLength()) {
        ADD_FAILURE() << decrypter->GetName() << " DecryptPacket length not equal";
        return false;
    }

    auto out_plaintext_span = out_plaintext->GetReadSpan();
    uint8_t* out_plaintext_pos = out_plaintext_span.GetStart();
    for (uint16_t i = 0; i < __plaintext_length; i++) {
        if (*(plaintext_pos + i) != *(out_plaintext_pos + i)) {
            ADD_FAILURE() << decrypter->GetName() << " DecryptPacket context not equal";
            return false;
        }
    }
    return true;
}

bool DecryptHeaderTest(std::shared_ptr<ICryptographer> encrypter, std::shared_ptr<ICryptographer> decrypter) {
    static const uint32_t __plaintext_length = 128;
    // make test plaintext
    uint8_t src_plaintext[__plaintext_length] = {0};
    auto plaintext_span = common::BufferSpan((uint8_t*)src_plaintext, (uint8_t*)src_plaintext + sizeof(src_plaintext));

    ShortHeader header;
    header.SetDestinationConnectionId((uint8_t*)__dest_connnection_id, sizeof(__dest_connnection_id));
    header.SetPacketNumberLength(2);

    std::shared_ptr<common::Buffer> plaintext = std::make_shared<common::Buffer>(plaintext_span);
    header.EncodeHeader(plaintext);

    uint8_t src_ciphertext[__plaintext_length] = {0};
    auto src_ciphertext_span = common::BufferSpan((uint8_t*)src_ciphertext, (uint8_t*)src_ciphertext + sizeof(src_ciphertext));
    memcpy(src_ciphertext_span.GetStart(), plaintext_span.GetStart(), __plaintext_length);
    
    common::BufferSpan sample_span = common::BufferSpan((uint8_t*)__sample, (uint8_t*)__sample + sizeof(__sample));
    uint64_t pn_offset = 2;

    if (!encrypter->EncryptHeader(src_ciphertext_span, sample_span, pn_offset, header.GetPacketNumberLength(), true)) {
        ADD_FAILURE() << encrypter->GetName() << " EncryptHeader failed";
        return false;
    }

    uint8_t pkt_number_len = 0;
    if (!decrypter->DecryptHeader(src_ciphertext_span, sample_span, pn_offset, pkt_number_len, true)) {
        ADD_FAILURE() << decrypter->GetName() << " DecryptHeader failed";
        return false;
    }

    if (header.GetPacketNumberLength() != pkt_number_len) {
        ADD_FAILURE() << decrypter->GetName() << " DecryptHeader packet number length not equal";
        return false;
    }

    for (uint16_t i = 0; i < __plaintext_length; i++) {
        if (src_plaintext[i] != src_ciphertext[i]) {
            ADD_FAILURE() << decrypter->GetName() << " DecryptHeader context not equal";
            return false;
        }
    }
    return true;
}

}
}