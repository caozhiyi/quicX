#include <openssl/aead.h>
#include <openssl/evp.h>

#include "quic/crypto/type.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/cryptographer_interface.h"

namespace quicx {

CryptographerIntreface::CryptographerIntreface() {

}

CryptographerIntreface::~CryptographerIntreface() {
    
}

bool CryptographerIntreface::Init(uint32_t cipher_id) {
    switch (cipher_id) {
    case TLS1_CK_AES_128_GCM_SHA256:
        _aead = EVP_aead_aes_128_gcm();
        _cipher = EVP_aes_128_ctr();
        _digest = EVP_sha256();
        break;

    case TLS1_CK_AES_256_GCM_SHA384:
        _aead = EVP_aead_aes_256_gcm();
        _cipher = EVP_aes_256_ctr();
        _digest = EVP_sha384();
        break;

    case TLS1_CK_CHACHA20_POLY1305_SHA256:
        _aead = EVP_aead_chacha20_poly1305();
        //_cipher = EVP_chacha20(); todo
        _digest = EVP_sha256();
        break;

    default:
        return false;
    }

    _aead_key_length = EVP_AEAD_key_length(_aead);
    _aead_iv_length = EVP_AEAD_nonce_length(_aead);

    _cipher_key_length = EVP_CIPHER_key_length(_cipher); 
    _cipher_iv_length = EVP_CIPHER_iv_length(_cipher);
    return true;
}

bool CryptographerIntreface::InstallSecret(uint8_t* secret, uint32_t secret_len, bool is_write) {
    Secret& dest_secret = is_write ? _write_secret : _read_secret;
    
    // make packet protect key
    dest_secret._key.resize(_aead_key_length);
    size_t len = 0;
    if (!Hkdf::HkdfExpand(&(*dest_secret._key.begin()), _aead_key_length,  secret, secret_len, __tls_label_key, sizeof(__tls_label_key) - 1, _digest)) {
        return false;
    }

    // make packet protect iv
    dest_secret._iv.resize(_aead_iv_length);
    if (!Hkdf::HkdfExpand(&(*dest_secret._iv.begin()), _aead_iv_length,  secret, secret_len, __tls_label_iv, sizeof(__tls_label_iv) - 1, _digest)) {
        return false;
    }

    // make header protext key
    dest_secret._hp.resize(_cipher_key_length);
    if (!Hkdf::HkdfExpand(&(*dest_secret._hp.begin()), _cipher_key_length,  secret, secret_len, __tls_label_hp, sizeof(__tls_label_hp) - 1, _digest)) {
        return false;
    }
    return true;
}

bool CryptographerIntreface::InstallInitSecret(uint8_t* secret, uint32_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) {
    const EVP_MD *digest = EVP_sha256();

    // make init secret
    uint8_t init_secret[__max_init_secret_length] = {0};
    if (!Hkdf::HkdfExtract(init_secret, __max_init_secret_length, secret, secret_len, salt, saltlen, digest)) {
        return false;
    }

    const uint8_t* read_label = is_server ? __tls_label_client : __tls_label_server;
    const uint8_t* write_label = is_server ? __tls_label_server : __tls_label_client;

    uint8_t init_read_secret[__max_init_secret_length] = {0};
    if (!Hkdf::HkdfExpand(init_read_secret, __max_init_secret_length,  init_secret,
        __max_init_secret_length, read_label, sizeof(read_label) - 1, digest)) {
        return false;
    }

    uint8_t init_write_secret[__max_init_secret_length] = {0};
    if (!Hkdf::HkdfExpand(init_write_secret, __max_init_secret_length,  init_secret,
        __max_init_secret_length, write_label, sizeof(write_label) - 1, digest)) {
        return false;
    }

    if (!InstallSecret(init_read_secret, __max_init_secret_length, false)) {
        return false;
    }

    if (!InstallSecret(init_write_secret, __max_init_secret_length, true)) {
        return false;
    }

    return true;
}

}
