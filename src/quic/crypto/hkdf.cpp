#include "third/boringssl/include/openssl/hkdf.h"
#include "quic/crypto/hkdf.h"


namespace quicx {
namespace quic {

bool Hkdf::HkdfExtract(uint8_t *dest, size_t destlen, const uint8_t *secret, size_t secretlen,
    const uint8_t *salt, size_t saltlen, const EVP_MD *md) {

    return 1 == HKDF_extract(dest, &destlen, md, secret, secretlen, salt, saltlen);
}

bool Hkdf::HkdfExpand(uint8_t *dest, size_t destlen, const uint8_t *secret, size_t secretlen,
    const uint8_t *info, size_t infolen, const EVP_MD *md) {

    return 1 == HKDF_expand(dest, destlen, md, secret, secretlen, info, infolen);
}

}
}
