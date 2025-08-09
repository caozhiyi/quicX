#include "third/boringssl/include/openssl/aead.h"
#include "third/boringssl/include/openssl/evp.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"

namespace quicx {
namespace quic {

Aes128GcmCryptographer::Aes128GcmCryptographer() {
    aead_ = EVP_aead_aes_128_gcm();
    cipher_ = EVP_aes_128_ctr();
    digest_ = EVP_sha256();

    aead_key_length_ = EVP_AEAD_key_length(aead_);
    aead_iv_length_ = EVP_AEAD_nonce_length(aead_);
    aead_tag_length_ = EVP_AEAD_max_tag_len(aead_);

    cipher_key_length_ = EVP_CIPHER_key_length(cipher_); 
    cipher_iv_length_ = EVP_CIPHER_iv_length(cipher_);
}

Aes128GcmCryptographer::~Aes128GcmCryptographer() {

}

const char* Aes128GcmCryptographer::GetName() {
    return "aes_128_gcm_cryptographer";
}

CryptographerId Aes128GcmCryptographer::GetCipherId() {
    return kCipherIdAes128GcmSha256;
}

}
}
