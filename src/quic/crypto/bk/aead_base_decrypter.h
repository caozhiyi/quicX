#ifndef QUIC_CRYPTO_AEAD_BASE_DECRYPTER
#define QUIC_CRYPTO_AEAD_BASE_DECRYPTER

#include <memory>

#include "openssl/aead.h"
#include "quic/crypto/decrypter_interface.h"

namespace quicx {

static const size_t __max_key_size = 32;
static const size_t __max_nonce_size = 12;

class IBufferReadOnly;
class AeadBaseDecrypter:
    public DecrypterIntreface {
public:
    AeadBaseDecrypter(const EVP_AEAD* (*aead_getter)(),
                    size_t key_size,
                    size_t auth_tag_size,
                    size_t nonce_size,
                    bool use_ietf_nonce_construction);
    ~AeadBaseDecrypter();

    bool SetSecret(const std::string& serret);

    bool SetIV(const std::string& iv);

    bool SetHeaderSecret(const std::string& secret);

    bool Decrypt(std::shared_ptr<IBufferReadOnly> buffer);

private:
    const EVP_AEAD* const _aead_alg;
    const size_t _key_size;
    const size_t _auth_tag_size;
    const size_t _nonce_size;
    // The key.
    unsigned char _key[__max_key_size];
    // The IV used to construct the nonce.
    unsigned char _iv[__max_nonce_size];

    EVP_AEAD_CTX *_ctx;
};

}

#endif