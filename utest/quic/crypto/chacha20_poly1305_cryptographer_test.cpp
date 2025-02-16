#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"
#include "utest/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {
namespace quic {
namespace {

TEST(ChaCha20Poly1305CryptographerTest, DecryptPacketInit) {
    std::shared_ptr<ChaCha20Poly1305Cryptographer> server_cryptographer = std::make_shared<ChaCha20Poly1305Cryptographer>();
    ASSERT_TRUE(server_cryptographer->InstallInitSecret(kDestConnnectionId, sizeof(kDestConnnectionId), kSalt, sizeof(kSalt), true));

    std::shared_ptr<ChaCha20Poly1305Cryptographer> client_cryptographer = std::make_shared<ChaCha20Poly1305Cryptographer>();
    ASSERT_TRUE(client_cryptographer->InstallInitSecret(kDestConnnectionId, sizeof(kDestConnnectionId), kSalt, sizeof(kSalt), false));

    ASSERT_TRUE(DecryptPacketTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptPacketTest(client_cryptographer, server_cryptographer));

    ASSERT_TRUE(DecryptHeaderTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(client_cryptographer, server_cryptographer));
}

TEST(ChaCha20Poly1305CryptographerTest, DecryptPacket) {
    std::shared_ptr<ChaCha20Poly1305Cryptographer> cryptographer = std::make_shared<ChaCha20Poly1305Cryptographer>();
    ASSERT_TRUE(cryptographer->InstallSecret(kSecret, sizeof(kSecret), true));
    ASSERT_TRUE(cryptographer->InstallSecret(kSecret, sizeof(kSecret), false));

    ASSERT_TRUE(DecryptPacketTest(cryptographer, cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(cryptographer, cryptographer));
}

}

}
}