#include <openssl/aead.h>
#include <openssl/evp.h>
#include "quic/crypto/aes_256_gcm_cryptographer.h"

namespace quicx {
namespace quic {

Aes256GcmCryptographer::Aes256GcmCryptographer() {
    aead_ = EVP_aead_aes_256_gcm();
    cipher_ = EVP_aes_256_ctr();
    digest_ = EVP_sha384();

    aead_key_length_ = EVP_AEAD_key_length(aead_);
    aead_iv_length_ = EVP_AEAD_nonce_length(aead_);
    aead_tag_length_ = EVP_AEAD_max_tag_len(aead_);

    cipher_key_length_ = EVP_CIPHER_key_length(cipher_); 
    cipher_iv_length_ = EVP_CIPHER_iv_length(cipher_);
}

Aes256GcmCryptographer::~Aes256GcmCryptographer() {

}

const char* Aes256GcmCryptographer::GetName() {
    return "aes_256_gcm_cryptographer";
}

CryptographerId Aes256GcmCryptographer::GetCipherId() {
    return CI_TLS1_CK_AES_256_GCM_SHA384;
}

}
}
