#include <openssl/aead.h>
#include <openssl/evp.h>

#include "common/log/log.h"
#include "common/buffer/buffer_interface.h"
#include "quic/crypto/type.h"
#include "quic/crypto/hkdf.h"
#include "quic/crypto/aead_base_cryptographer.h"

namespace quicx {

AeadBaseCryptographer::AeadBaseCryptographer():
    _digest(nullptr),
    _aead(nullptr),
    _cipher(nullptr),
    _write_aead_ctx(nullptr),
    _read_aead_ctx(nullptr) {

}

AeadBaseCryptographer::~AeadBaseCryptographer() {
    if (_write_aead_ctx) {
        EVP_AEAD_CTX_free(_write_aead_ctx);
    }
    if (_read_aead_ctx) {
        EVP_AEAD_CTX_free(_read_aead_ctx);
    }
}

bool AeadBaseCryptographer::InstallSecret(uint8_t* secret, uint32_t secret_len, bool is_write) {
    Secret& dest_secret = is_write ? _write_secret : _read_secret;
    
    // make packet protect key
    dest_secret._key.resize(_aead_key_length);
    size_t len = 0;
    if (!Hkdf::HkdfExpand(dest_secret._key.data(), _aead_key_length,  secret, secret_len, __tls_label_key, sizeof(__tls_label_key) - 1, _digest)) {
        return false;
    }

    // make packet protect iv
    dest_secret._iv.resize(_aead_iv_length);
    if (!Hkdf::HkdfExpand(dest_secret._iv.data(), _aead_iv_length,  secret, secret_len, __tls_label_iv, sizeof(__tls_label_iv) - 1, _digest)) {
        return false;
    }

    // make header protext key
    dest_secret._hp.resize(_cipher_key_length);
    if (!Hkdf::HkdfExpand(dest_secret._hp.data(), _cipher_key_length,  secret, secret_len, __tls_label_hp, sizeof(__tls_label_hp) - 1, _digest)) {
        return false;
    }
    return InitKey(is_write);
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

bool AeadBaseCryptographer::DecryptPacket(uint64_t pn, BufferView associated_data, std::shared_ptr<IBufferReadOnly> ciphertext,
    std::shared_ptr<IBufferReadOnly> out_plaintext) {
    return true;
}

bool AeadBaseCryptographer::EncryptPacket(uint64_t pn, BufferView associated_data, std::shared_ptr<IBufferReadOnly> plaintext,
    std::shared_ptr<IBufferReadOnly> out_ciphertext) {
    return true;
}

bool AeadBaseCryptographer::DecryptHeader(std::shared_ptr<IBufferReadOnly> ciphertext, uint8_t pn_offset, bool is_short) {
    return true;
}

bool AeadBaseCryptographer::EncryptHeader(std::shared_ptr<IBufferReadOnly> plaintext, uint8_t pn_offset, bool is_short) {
    return true;
}

bool AeadBaseCryptographer::InitKey(bool is_write) {
    EVP_AEAD_CTX* ctx = is_write ? _write_aead_ctx : _read_aead_ctx;
    if (ctx) {
        EVP_AEAD_CTX_cleanup(ctx);
    }

    Secret& secret = is_write ? _write_secret : _read_secret;
    if (!EVP_AEAD_CTX_init(ctx, _aead, secret._key.data(), secret._key.size(), _aead_tag_length,
                         nullptr)) {
        LOG_ERROR("EVP_AEAD_CTX_init failed");
        return false;
    }
    return true;
}

}
