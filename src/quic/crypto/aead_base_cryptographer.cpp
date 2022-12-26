#include "quic/crypto/type.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {

AeadBaseCryptographer::AeadBaseCryptographer() {

}

AeadBaseCryptographer::~AeadBaseCryptographer() {

}

bool AeadBaseCryptographer::InstallSecret(uint8_t* secret, uint32_t secret_len, bool is_write) {
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

bool AeadBaseCryptographer::InstallInitSecret(uint8_t* secret, uint32_t secret_len, const uint8_t *salt, size_t saltlen, bool is_server) {
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

bool AeadBaseCryptographer::DecryptPacket(std::shared_ptr<IBufferReadOnly> ciphertext,
    std::shared_ptr<IBufferReadOnly> out_plaintext) {
    return true;
}

bool AeadBaseCryptographer::EncryptPacket(std::shared_ptr<IBufferReadOnly> plaintext,
    std::shared_ptr<IBufferReadOnly> out_ciphertext) {
    return true;
}


}
