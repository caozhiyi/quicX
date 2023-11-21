#include <openssl/aead.h>
#include <openssl/evp.h>
#include "quic/crypto/aes_128_gcm_cryptographer.h"

namespace quicx {
namespace quic {

Aes128GcmCryptographer::Aes128GcmCryptographer() {
    _aead = EVP_aead_aes_128_gcm();
    _cipher = EVP_aes_128_ctr();
    _digest = EVP_sha256();

    _aead_key_length = EVP_AEAD_key_length(_aead);
    _aead_iv_length = EVP_AEAD_nonce_length(_aead);
    _aead_tag_length = EVP_AEAD_max_tag_len(_aead);

    _cipher_key_length = EVP_CIPHER_key_length(_cipher); 
    _cipher_iv_length = EVP_CIPHER_iv_length(_cipher);
}

Aes128GcmCryptographer::~Aes128GcmCryptographer() {

}

const char* Aes128GcmCryptographer::GetName() {
    return "aes_128_gcm_cryptographer";
}

CryptographerId Aes128GcmCryptographer::GetCipherId() {
    return CI_TLS1_CK_AES_128_GCM_SHA256;
}

}
}
