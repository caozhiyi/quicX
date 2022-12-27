#include <openssl/aead.h>
#include <openssl/evp.h>
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

namespace quicx {

ChaCha20Poly1305Cryptographer::ChaCha20Poly1305Cryptographer() {
    _aead = EVP_aead_chacha20_poly1305();
    //_cipher = EVP_chacha20(); todo
    _digest = EVP_sha256();

    _aead_key_length = EVP_AEAD_key_length(_aead);
    _aead_iv_length = EVP_AEAD_nonce_length(_aead);
    _aead_tag_length = EVP_AEAD_max_tag_len(_aead);

    //_cipher_key_length = EVP_CIPHER_key_length(_cipher); 
    //_cipher_iv_length = EVP_CIPHER_iv_length(_cipher);
}

ChaCha20Poly1305Cryptographer::~ChaCha20Poly1305Cryptographer() {

}

const char* ChaCha20Poly1305Cryptographer::GetName() {
    return "chacha20_poly1305_cryptographer";
}

uint32_t ChaCha20Poly1305Cryptographer::GetCipherId() {
    return TLS1_CK_CHACHA20_POLY1305_SHA256;
}


}
