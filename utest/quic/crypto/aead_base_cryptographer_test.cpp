#include <cstring>
#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_read_view.h"
#include "common/buffer/buffer_read_write.h"
#include "common/buffer/buffer_write_view.h"
#include "utest/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {

bool DecryptPacketTest(std::shared_ptr<ICryptographer> encrypter, std::shared_ptr<ICryptographer> decrypter) {
    std::shared_ptr<BlockMemoryPool> pool = std::make_shared<BlockMemoryPool>(2048, 5);

    static const uint32_t __plaintext_length = 1024;
    
    // make test plaintext
    std::shared_ptr<BufferReadWrite> plaintext = std::make_shared<BufferReadWrite>(pool);
    auto plaintext_pair = plaintext->GetWritePair();
    for (uint16_t i = 0; i < __plaintext_length; i++) {
        *(plaintext_pair.first + i) = i;
    }
    plaintext->MoveWritePt(__plaintext_length);

    uint64_t pkt_num = 102154;
    std::shared_ptr<BufferReadWrite> out_ciphertext = std::make_shared<BufferReadWrite>(pool);
    if (!encrypter->EncryptPacket(pkt_num, BufferReadView(__associated_data, __associated_data + sizeof(__associated_data)),
        plaintext, out_ciphertext)) {
        ADD_FAILURE() << encrypter->GetName() << " EncryptPacket failed";
        return false;
    }
    
    std::shared_ptr<BufferReadWrite> out_plaintext = std::make_shared<BufferReadWrite>(pool);
    if (!decrypter->DecryptPacket(pkt_num, BufferReadView(__associated_data,  __associated_data + sizeof(__associated_data)),
        out_ciphertext->GetReadViewPtr(), out_plaintext)) {
        ADD_FAILURE() << decrypter->GetName() << " DecryptPacket failed";
        return false;
    }

    if (plaintext->GetDataLength() != out_plaintext->GetDataLength()) {
        ADD_FAILURE() << decrypter->GetName() << " DecryptPacket length not equal";
        return false;
    }

    auto out_plaintext_pair = out_plaintext->GetReadPair();
    for (uint16_t i = 0; i < __plaintext_length; i++) {
        if (*(plaintext_pair.first + i) != *(out_plaintext_pair.first + i)) {
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
    std::shared_ptr<BufferReadWrite> plaintext = std::make_shared<BufferReadWrite>(pool);
    auto plaintext_pair = plaintext->GetWritePair();
    for (uint16_t i = 0; i < __plaintext_length; i++) {
        *(plaintext_pair.first + i) = i;
    }
    plaintext->MoveWritePt(__plaintext_length);

    std::shared_ptr<BufferReadWrite> src_ciphertext = std::make_shared<BufferReadWrite>(pool);
    auto src_write_pair = src_ciphertext->GetWritePair();
    memcpy(src_write_pair.first, plaintext_pair.first, __plaintext_length);
    src_ciphertext->MoveWritePt(__plaintext_length);

    uint64_t pkt_num = 102154;
    uint64_t pn_offset = 2;
    uint64_t pkt_length = 1;
    if (!encrypter->EncryptHeader(plaintext, pn_offset, pkt_length, true)) {
        ADD_FAILURE() << encrypter->GetName() << " EncryptHeader failed";
        return false;
    }

    if (!decrypter->DecryptHeader(plaintext, pn_offset, true)) {
        ADD_FAILURE() << encrypter->GetName() << " DecryptHeader failed";
        return false;
    }

    for (uint16_t i = 0; i < __plaintext_length; i++) {
        if (*(plaintext_pair.first + i) != *(src_write_pair.first + i)) {
            ADD_FAILURE() << decrypter->GetName() << " DecryptHeader context not equal";
            return false;
        }
    }
    return true;
}

}