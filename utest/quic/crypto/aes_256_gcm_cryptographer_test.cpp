#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "common/buffer/buffer_readonly.h"
#include "quic/crypto/aes_256_gcm_cryptographer.h"
#include "utest/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {
namespace {

TEST(Aes256GcmCryptographerTest, DecryptPacketInit) {
    std::shared_ptr<Aes256GcmCryptographer> server_cryptographer = std::make_shared<Aes256GcmCryptographer>();
    ASSERT_TRUE(server_cryptographer->InstallInitSecret(__dest_connnection_id, sizeof(__dest_connnection_id), __salt, sizeof(__salt), true));

    std::shared_ptr<Aes256GcmCryptographer> client_cryptographer = std::make_shared<Aes256GcmCryptographer>();
    ASSERT_TRUE(client_cryptographer->InstallInitSecret(__dest_connnection_id, sizeof(__dest_connnection_id), __salt, sizeof(__salt), false));

    ASSERT_TRUE(DecryptPacketTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptPacketTest(client_cryptographer, server_cryptographer));

    ASSERT_TRUE(DecryptHeaderTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(client_cryptographer, server_cryptographer));
}

TEST(Aes256GcmCryptographerTest, DecryptPacket) {
    std::shared_ptr<Aes256GcmCryptographer> cryptographer = std::make_shared<Aes256GcmCryptographer>();
    ASSERT_TRUE(cryptographer->InstallSecret(__secret, sizeof(__secret), true));
    ASSERT_TRUE(cryptographer->InstallSecret(__secret, sizeof(__secret), false));

    ASSERT_TRUE(DecryptPacketTest(cryptographer, cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(cryptographer, cryptographer));
}

}

}