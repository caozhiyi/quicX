#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"
#include "utest/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {
namespace quic {
namespace {

TEST(ChaCha20Poly1305CryptographerTest, DecryptPacketInit) {
    std::shared_ptr<ChaCha20Poly1305Cryptographer> server_cryptographer = std::make_shared<ChaCha20Poly1305Cryptographer>();
    ASSERT_EQ(server_cryptographer->InstallInitSecret(kDestConnnectionId, sizeof(kDestConnnectionId), kSalt, sizeof(kSalt), true), ICryptographer::Result::kOk);

    std::shared_ptr<ChaCha20Poly1305Cryptographer> client_cryptographer = std::make_shared<ChaCha20Poly1305Cryptographer>();
    ASSERT_EQ(client_cryptographer->InstallInitSecret(kDestConnnectionId, sizeof(kDestConnnectionId), kSalt, sizeof(kSalt), false), ICryptographer::Result::kOk);

    ASSERT_TRUE(DecryptPacketTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptPacketTest(client_cryptographer, server_cryptographer));

    ASSERT_TRUE(DecryptHeaderTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(client_cryptographer, server_cryptographer));
}

TEST(ChaCha20Poly1305CryptographerTest, DecryptPacket) {
    std::shared_ptr<ChaCha20Poly1305Cryptographer> cryptographer = std::make_shared<ChaCha20Poly1305Cryptographer>();
    ASSERT_EQ(cryptographer->InstallSecret(kSecret, sizeof(kSecret), true), ICryptographer::Result::kOk);
    ASSERT_EQ(cryptographer->InstallSecret(kSecret, sizeof(kSecret), false), ICryptographer::Result::kOk);

    ASSERT_TRUE(DecryptPacketTest(cryptographer, cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(cryptographer, cryptographer));
}

}

}
}