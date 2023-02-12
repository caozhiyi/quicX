#include <openssl/ssl.h>
#include <openssl/tls1.h>
#include "common/log/log.h"
#include "quic/crypto/cryptographer_interface.h"
#include "quic/crypto/aes_128_gcm_cryptographer.h"
#include "quic/crypto/aes_256_gcm_cryptographer.h"
#include "quic/crypto/chacha20_poly1305_cryptographer.h"

namespace quicx {

ICryptographer::ICryptographer() {

}

ICryptographer::~ICryptographer() {
    
}

std::shared_ptr<ICryptographer> MakeCryptographer(const SSL_CIPHER *cipher) {
    uint32_t cipher_id = SSL_CIPHER_get_id(cipher);
    return MakeCryptographer(cipher_id);
}

std::shared_ptr<ICryptographer> MakeCryptographer(uint32_t cipher_id) {
    switch (cipher_id)
    {
    case TLS1_CK_AES_128_GCM_SHA256:
        return std::make_shared<Aes128GcmCryptographer>();
    case TLS1_CK_AES_256_GCM_SHA384:
        return std::make_shared<Aes256GcmCryptographer>();
    case TLS1_CK_CHACHA20_POLY1305_SHA256:
        return std::make_shared<ChaCha20Poly1305Cryptographer>();
    default:
        LOG_ERROR("unsupport cipher id. id:%d", cipher_id);
        break;
    }
    return nullptr;
}

}
