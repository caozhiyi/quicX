#include <cstring>
#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/buffer.h"
#include "common/buffer/buffer_write_view.h"
#include "utest/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {

bool DecryptPacketTest(std::shared_ptr<ICryptographer> encrypter, std::shared_ptr<ICryptographer> decrypter) {
    std::shared_ptr<BlockMemoryPool> pool = std::make_shared<BlockMemoryPool>(2048, 5);

    static const uint32_t __plaintext_length = 1024;
    
    // make test plaintext
    std::shared_ptr<Buffer> plaintext = std::make_shared<Buffer>(pool);
    auto plaintext_span = plaintext->GetWriteSpan();
    uint8_t* plaintext_pos = plaintext_span.GetStart();
    for (uint16_t i = 0; i < __plaintext_length; i++) {
        *(plaintext_pos + i) = i;
    }
    plaintext->MoveWritePt(__plaintext_length);

    uint64_t pkt_num = 102154;
    std::shared_ptr<Buffer> out_ciphertext = std::make_shared<Buffer>(pool);
    BufferSpan associated_data_span = BufferSpan((uint8_t*)__associated_data, (uint8_t*)__associated_data + sizeof(__associated_data));
    BufferSpan plaintext_span2 = plaintext->GetReadSpan();
    if (!encrypter->EncryptPacket(pkt_num, associated_data_span, plaintext_span2, out_ciphertext)) {
        ADD_FAILURE() << encrypter->GetName() << " EncryptPacket failed";
        return false;
    }
    
    std::shared_ptr<Buffer> out_plaintext = std::make_shared<Buffer>(pool);
    BufferSpan associated_data_span1 = BufferSpan((uint8_t*)__associated_data,  (uint8_t*)__associated_data + sizeof(__associated_data));
    BufferSpan plaintext_span1 = out_ciphertext->GetReadSpan();
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
    std::shared_ptr<BlockMemoryPool> pool = std::make_shared<BlockMemoryPool>(2048, 5);

    static const uint32_t __plaintext_length = 5;
    // make test plaintext
    std::shared_ptr<Buffer> plaintext = std::make_shared<Buffer>(pool);
    auto plaintext_span = plaintext->GetWriteSpan();
    uint8_t* plaintext_pos = plaintext_span.GetStart();
    for (uint16_t i = 0; i < __plaintext_length; i++) {
        *(plaintext_pos + i) = i;
    }
    plaintext->MoveWritePt(__plaintext_length);

    std::shared_ptr<Buffer> src_ciphertext = std::make_shared<Buffer>(pool);
    auto plsrc_write_span = src_ciphertext->GetWriteSpan();
    memcpy(plsrc_write_span.GetStart(), plaintext_span.GetStart(), __plaintext_length);
    src_ciphertext->MoveWritePt(__plaintext_length);
    uint8_t* plsrc_write_pos = plsrc_write_span.GetStart();

    auto span = plaintext->GetReadSpan();
    uint64_t pkt_num = 102154;
    uint64_t pn_offset = 2;
    uint64_t pkt_length = 1;
    /*
    if (!encrypter->EncryptHeader(span, pn_offset, pkt_length, true)) {
        ADD_FAILURE() << encrypter->GetName() << " EncryptHeader failed";
        return false;
    }

    if (!decrypter->DecryptHeader(span, pn_offset, true)) {
        ADD_FAILURE() << encrypter->GetName() << " DecryptHeader failed";
        return false;
    }

    for (uint16_t i = 0; i < __plaintext_length; i++) {
        if (*(plaintext_pos + i) != *(plsrc_write_pos + i)) {
            ADD_FAILURE() << decrypter->GetName() << " DecryptHeader context not equal";
            return false;
        }
    }*/
    return true;
}

}