#include <gtest/gtest.h>
#include "quic/crypto/aes_256_gcm_cryptographer.h"
#include "unit_test/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {
namespace quic {
namespace {

TEST(Aes256GcmCryptographerTest, DecryptPacketInit) {
    std::shared_ptr<Aes256GcmCryptographer> server_cryptographer = std::make_shared<Aes256GcmCryptographer>();
    ASSERT_EQ(server_cryptographer->InstallInitSecret(kDestConnnectionId, sizeof(kDestConnnectionId), kSalt, sizeof(kSalt), true), ICryptographer::Result::kOk);

    std::shared_ptr<Aes256GcmCryptographer> client_cryptographer = std::make_shared<Aes256GcmCryptographer>();
    ASSERT_EQ(client_cryptographer->InstallInitSecret(kDestConnnectionId, sizeof(kDestConnnectionId), kSalt, sizeof(kSalt), false), ICryptographer::Result::kOk);

    ASSERT_TRUE(DecryptPacketTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptPacketTest(client_cryptographer, server_cryptographer));

    ASSERT_TRUE(DecryptHeaderTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(client_cryptographer, server_cryptographer));
}

TEST(Aes256GcmCryptographerTest, DecryptPacket) {
    std::shared_ptr<Aes256GcmCryptographer> cryptographer = std::make_shared<Aes256GcmCryptographer>();
    ASSERT_EQ(cryptographer->InstallSecret(kSecret, sizeof(kSecret), true), ICryptographer::Result::kOk);
    ASSERT_EQ(cryptographer->InstallSecret(kSecret, sizeof(kSecret), false), ICryptographer::Result::kOk);

    ASSERT_TRUE(DecryptPacketTest(cryptographer, cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(cryptographer, cryptographer));
}

}

}
}