#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_readonly.h"
#include "utest/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {

bool DecryptPacketTest(std::shared_ptr<CryptographerIntreface> encrypter, std::shared_ptr<CryptographerIntreface> decrypter) {
    std::shared_ptr<BlockMemoryPool> pool = std::make_shared<BlockMemoryPool>(2048, 5);

    // make test plaintext
    std::shared_ptr<IBufferReadOnly> plaintext = std::make_shared<BufferReadOnly>(pool);
    auto write_pair = plaintext->GetWritePair();
    for (uint16_t i = 0; i < 1024; i++) {
        *(write_pair.first + i) = i;
    }
    plaintext->MoveWritePt(1024);

    uint64_t pkt_num = 102154;
    std::shared_ptr<IBufferReadOnly> out_ciphertext = std::make_shared<BufferReadOnly>(pool);
    if (!encrypter->EncryptPacket(pkt_num, BufferView(__associated_data, sizeof(__associated_data)), plaintext, out_ciphertext)) {
        ADD_FAILURE() << encrypter->GetName() << " EncryptPacket failed";
        return false;
    }
    
    std::shared_ptr<IBufferReadOnly> out_plaintext = std::make_shared<BufferReadOnly>(pool);
    if (!decrypter->DecryptPacket(pkt_num, BufferView(__associated_data, sizeof(__associated_data)), out_ciphertext, out_plaintext)) {
        ADD_FAILURE() << decrypter->GetName() << " DecryptPacket failed";
        return false;
    }

    if (plaintext->GetCanReadLength() != out_plaintext->GetCanReadLength()) {
        ADD_FAILURE() << decrypter->GetName() << " DecryptPacket length not equal";
        return false;
    }
    auto read_pair = plaintext->GetReadPair();
    for (uint16_t i = 0; i < 1024; i++) {
        if (*(write_pair.first + i) != *(read_pair.first + i)) {
            ADD_FAILURE() << decrypter->GetName() << " DecryptPacket context not equal";
            return false;
        }
    }
    return true;
}

bool DecryptHeaderTest(std::shared_ptr<CryptographerIntreface> encrypter, std::shared_ptr<CryptographerIntreface> decrypter) {
    std::shared_ptr<BlockMemoryPool> pool = std::make_shared<BlockMemoryPool>(2048, 5);

    static const uint32_t __plaintext_length = 5;
    // make test plaintext
    std::shared_ptr<IBufferReadOnly> plaintext = std::make_shared<BufferReadOnly>(pool);
    auto write_pair = plaintext->GetWritePair();
    for (uint16_t i = 0; i < __plaintext_length; i++) {
        *(write_pair.first + i) = i;
    }
    plaintext->MoveWritePt(__plaintext_length);

    std::shared_ptr<IBufferReadOnly> src_ciphertext = std::make_shared<BufferReadOnly>(pool);
    auto src_write_pair = plaintext->GetWritePair();
    memcpy(src_write_pair.first, write_pair.first, __plaintext_length);
    src_ciphertext->MoveWritePt(__plaintext_length);

    uint64_t pkt_num = 102154;
    uint64_t pn_offset = 2;
    uint64_t pkt_length = 3;
    if (!encrypter->EncryptHeader(plaintext, pn_offset, pkt_length, true)) {
        ADD_FAILURE() << encrypter->GetName() << " EncryptHeader failed";
        return false;
    }

    if (!encrypter->DecryptHeader(plaintext, pn_offset, true)) {
        ADD_FAILURE() << encrypter->GetName() << " DecryptHeader failed";
        return false;
    }

    for (uint16_t i = 0; i < __plaintext_length; i++) {
        if (*(write_pair.first + i) != *(src_write_pair.first + i)) {
            ADD_FAILURE() << decrypter->GetName() << " DecryptHeader context not equal";
            return false;
        }
    }
    return true;
}

}