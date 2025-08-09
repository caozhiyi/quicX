#ifndef QUIC_CRYPTO_TYPE
#define QUIC_CRYPTO_TYPE

#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include "common/util/c_smart_ptr.h"

namespace quicx {
namespace quic {

// hkdf expand label (constexpr safer literals) with exact lengths
constexpr std::array<uint8_t, 15> kTlsLabelClient = {
  't','l','s','1','3',' ','c','l','i','e','n','t',' ','i','n'
};
constexpr std::array<uint8_t, 15> kTlsLabelServer = {
  't','l','s','1','3',' ','s','e','r','v','e','r',' ','i','n'
};
constexpr std::array<uint8_t, 14> kTlsLabelKey = {
  't','l','s','1','3',' ','q','u','i','c',' ','k','e','y'
};
constexpr std::array<uint8_t, 13> kTlsLabelHp = {
  't','l','s','1','3',' ','q','u','i','c',' ','h','p'
};
constexpr std::array<uint8_t, 13> kTlsLabelIv = {
  't','l','s','1','3',' ','q','u','i','c',' ','i','v'
};
// Key Update label per RFC 9001
constexpr std::array<uint8_t, 13> kTlsLabelKu = {
  't','l','s','1','3',' ','q','u','i','c',' ','k','u'
};
constexpr std::array<uint8_t, 5>  kHeaderMask     = { 0,0,0,0,0 };

constexpr size_t kMaxInitSecretLength       = 32;
constexpr size_t kHeaderProtectSampleLength = 16;
constexpr size_t kHeaderProtectMaskLength   = 5;
constexpr size_t kPacketNonceLength         = 16;
constexpr size_t kCryptoLevelCount          = 4;

constexpr std::array<uint8_t, 20> kInitialSalt = { 0x38,0x76,0x2c,0xf7,0xf5,0x59,0x34,0xb3,0x4d,0x17,0x9a,0xe6,0xa4,0xc8,0x0c,0xad,0xcc,0xbb,0x7f,0x0a };

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