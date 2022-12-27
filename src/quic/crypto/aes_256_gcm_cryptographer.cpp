#include <openssl/aead.h>
#include <openssl/evp.h>
#include "quic/crypto/aes_256_gcm_cryptographer.h"

namespace quicx {

Aes256GcmCryptographer::Aes256GcmCryptographer() {
    _aead = EVP_aead_aes_256_gcm();
    _cipher = EVP_aes_256_ctr();
    _digest = EVP_sha384();

    _aead_key_length = EVP_AEAD_key_length(_aead);
    _aead_iv_length = EVP_AEAD_nonce_length(_aead);
    _aead_tag_length = EVP_AEAD_max_tag_len(_aead);

    _cipher_key_length = EVP_CIPHER_key_length(_cipher); 
    _cipher_iv_length = EVP_CIPHER_iv_length(_cipher);
}

Aes256GcmCryptographer::~Aes256GcmCryptographer() {

}

const char* Aes256GcmCryptographer::GetName() {
    return "aes_256_gcm_cryptographer";
}

uint32_t Aes256GcmCryptographer::GetCipherId() {
    return TLS1_CK_AES_256_GCM_SHA384;
}

}
