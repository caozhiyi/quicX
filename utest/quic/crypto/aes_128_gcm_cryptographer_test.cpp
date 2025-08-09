#include <gtest/gtest.h>
#include "common/alloter/pool_block.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "utest/quic/crypto/aead_base_cryptographer_test.h"

namespace quicx {
namespace quic {
namespace {

TEST(Aes128GcmCryptographerTest, DecryptPacketInit) {
    std::shared_ptr<Aes128GcmCryptographer> server_cryptographer = std::make_shared<Aes128GcmCryptographer>();
    ASSERT_EQ(server_cryptographer->InstallInitSecret(kDestConnnectionId, sizeof(kDestConnnectionId), kSalt, sizeof(kSalt), true), ICryptographer::Result::kOk);

    std::shared_ptr<Aes128GcmCryptographer> client_cryptographer = std::make_shared<Aes128GcmCryptographer>();
    ASSERT_EQ(client_cryptographer->InstallInitSecret(kDestConnnectionId, sizeof(kDestConnnectionId), kSalt, sizeof(kSalt), false), ICryptographer::Result::kOk);

    ASSERT_TRUE(DecryptPacketTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptPacketTest(client_cryptographer, server_cryptographer));

    ASSERT_TRUE(DecryptHeaderTest(server_cryptographer, client_cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(client_cryptographer, server_cryptographer));
}

TEST(Aes128GcmCryptographerTest, DecryptPacket) {
    std::shared_ptr<Aes128GcmCryptographer> cryptographer = std::make_shared<Aes128GcmCryptographer>();
    ASSERT_EQ(cryptographer->InstallSecret(kSecret, sizeof(kSecret), true), ICryptographer::Result::kOk);
    ASSERT_EQ(cryptographer->InstallSecret(kSecret, sizeof(kSecret), false), ICryptographer::Result::kOk);

    ASSERT_TRUE(DecryptPacketTest(cryptographer, cryptographer));
    ASSERT_TRUE(DecryptHeaderTest(cryptographer, cryptographer));
}

}

}
}