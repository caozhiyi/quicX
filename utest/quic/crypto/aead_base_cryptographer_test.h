#ifndef UTEST_QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER_TEST
#define UTEST_QUIC_CRYPTO_AEAD_BASE_CRYPTOGRAPHER_TEST

#include <cstdint>
#include "quic/crypto/cryptographer_interface.h"

namespace quicx {

static const uint8_t __dest_connnection_id[] = "cf06";
static const uint8_t __salt[] = "\x38\x76\x2c\xf7\xf5\x59\x34\xb3\x4d\x17\x9a\xe6\xa4\xc8\x0c\xad\xcc\xbb\x7f\x0a"; 
static const uint8_t __secret[] = "cf063a34d4a9a76c2c86787d3f96db71";
static const uint8_t __associated_data[] = "cf063a34d4a9a76c2c86787d3f96db71cf063a34d4a9a76c2c86787d3f96db71cf063a34d4a9a76c2c86787d3f96db71cf063a34d4a9a76c2c86787d3f96db71";

bool DecryptPacketTest(std::shared_ptr<ICryptographer> encrypter, std::shared_ptr<ICryptographer> decrypter);
bool DecryptHeaderTest(std::shared_ptr<ICryptographer> encrypter, std::shared_ptr<ICryptographer> decrypter);

}

#endif