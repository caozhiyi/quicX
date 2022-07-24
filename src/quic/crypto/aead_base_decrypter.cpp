#include <cstring>

#include "openssl/err.h"
#include "openssl/evp.h"
#include "openssl/crypto.h"

#include "common/log/log.h"
#include "quic/crypto/aead_base_decrypter.h"


namespace quicx {

const EVP_AEAD* InitAndCall(const EVP_AEAD* (*aead_getter)()) {
  // Ensure BoringSSL is initialized before calling |aead_getter|. In Chromium,
  // the static initializer is disabled.
  CRYPTO_library_init();
  return aead_getter();
}


AeadBaseDecrypter::AeadBaseDecrypter(const EVP_AEAD* (*aead_getter)(),
                    size_t key_size,
                    size_t auth_tag_size,
                    size_t nonce_size,
                    bool use_ietf_nonce_construction):
    _aead_alg(InitAndCall(aead_getter)),
    _key_size(key_size),
    _auth_tag_size(auth_tag_size),
    _nonce_size(nonce_size){

}

AeadBaseDecrypter::~AeadBaseDecrypter() {

}

bool AeadBaseDecrypter::SetSecret(const std::string& serret) {
    if (serret.size() != _key_size) {
        return false;
    }
    memcpy(_key, serret.data(), serret.size());

    EVP_AEAD_CTX_cleanup(_ctx);
    if (!EVP_AEAD_CTX_init(_ctx, _aead_alg, _key, _key_size, _auth_tag_size,
                         nullptr)) {
        LOG_ERROR("EVP_AEAD_CTX_init failed");
        return false;
    }
    return true;
}

bool AeadBaseDecrypter::SetIV(const std::string& iv) {
    if (iv.size() != _nonce_size) {
        return false;
    }
    memcpy(_iv, iv.data(), iv.size());
    return true;
}

bool AeadBaseDecrypter::SetHeaderSecret(const std::string& secret) {
    return true;
}

bool AeadBaseDecrypter::Decrypt(std::shared_ptr<IBufferReadOnly> buffer) {
    return true;
}

}