#ifndef QUIC_CRYPTO_TYPE
#define QUIC_CRYPTO_TYPE

#include <cstdint>
#include <openssl/evp.h>
#include "common/util/c_smart_ptr.h"

namespace quicx {
namespace quic {

// hkdf expand label
static const uint8_t kTlsLabelClient[] = "tls13 client in";
static const uint8_t kTlsLabelServer[] = "tls13 server in";
static const uint8_t kTlsLabelKey[]    = "tls13 quic key";
static const uint8_t kTlsLabelHp[]     = "tls13 quic hp";
static const uint8_t kTlsLabelIv[]     = "tls13 quic iv";
static const uint8_t kHeaderMask[]     = "\x00\x00\x00\x00\x00";

static const uint16_t kMaxInitSecretLength       = 32;
static const uint16_t kHeaderProtectSampleLength = 16;
static const uint16_t kHeaderProtectMaskLength   = 5;
static const uint16_t kPacketNonceLength         = 16;
static const uint16_t kCryptoLevelCount          = 4;

static const uint8_t kInitialSalt[] = "\x38\x76\x2c\xf7\xf5\x59\x34\xb3\x4d\x17\x9a\xe6\xa4\xc8\x0c\xad\xcc\xbb\x7f\x0a";

enum CryptographerId: uint16_t {
    kCipherIdAes128GcmSha256,
    kCipherIdAes256GcmSha384,
    kCipherIdChaCha20Poly1305Sha256,
};

using EVPCIPHERCTXPtr = common::CSmartPtr<EVP_CIPHER_CTX, EVP_CIPHER_CTX_free>;
using EVPAEADCTXPtr = common::CSmartPtr<EVP_AEAD_CTX, EVP_AEAD_CTX_free>;

}
}

#endif