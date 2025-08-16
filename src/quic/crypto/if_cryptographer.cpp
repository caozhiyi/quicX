#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include "common/log/log.h"
#include "quic/crypto/if_cryptographer.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/crypto/aes_256_gcm_cryptographer.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

namespace quicx {
namespace quic {

ICryptographer::ICryptographer() {

}

ICryptographer::~ICryptographer() {
    
}

CryptographerId ICryptographer::AdapterCryptographerType(uint32_t cipher_id) {
    switch (cipher_id)
    {
    case TLS1_CK_AES_128_GCM_SHA256: return kCipherIdAes128GcmSha256;
    case TLS1_CK_AES_256_GCM_SHA384: return kCipherIdAes256GcmSha384;
    case TLS1_CK_CHACHA20_POLY1305_SHA256: return kCipherIdChaCha20Poly1305Sha256;
    default:
        common::LOG_ERROR("unknow cipher. id:%d", cipher_id);
        abort();
    }
}

std::shared_ptr<ICryptographer> MakeCryptographer(const SSL_CIPHER *cipher) {
    return MakeCryptographer(ICryptographer::AdapterCryptographerType(SSL_CIPHER_get_id(cipher)));
}

std::shared_ptr<ICryptographer> MakeCryptographer(CryptographerId cipher) {
    switch (cipher)
    {
    case kCipherIdAes128GcmSha256:
        return std::make_shared<Aes128GcmCryptographer>();
    case kCipherIdAes256GcmSha384:
        return std::make_shared<Aes256GcmCryptographer>();
    case kCipherIdChaCha20Poly1305Sha256:
        return std::make_shared<ChaCha20Poly1305Cryptographer>();
    default:
        common::LOG_ERROR("unsupport cipher id. id:%d", cipher);
        break;
    }
    return nullptr;
}

}
}
