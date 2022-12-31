#ifndef QUIC_CRYPTO_HKDF
#define QUIC_CRYPTO_HKDF

#include <cstdint>
#include <openssl/ossl_typ.h>

namespace quicx {

class Hkdf {
public:
    Hkdf() {}
    virtual ~Hkdf() {}

    static bool HkdfExtract(uint8_t *dest, size_t destlen, const uint8_t *secret, size_t secretlen,
        const uint8_t *salt, size_t saltlen, const EVP_MD *md);

    static bool HkdfExpand(uint8_t *dest, size_t destlen, const uint8_t *secret, size_t secretlen,
        const uint8_t *info, size_t infolen, const EVP_MD *md);
};

}

#endif