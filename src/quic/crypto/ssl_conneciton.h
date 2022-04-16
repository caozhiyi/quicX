#ifndef QUIC_CRYPTO_SSL_CONNECTION
#define QUIC_CRYPTO_SSL_CONNECTION

#include <string>
#include <cstdint>
#include "openssl/ssl.h"
#include "common/util/singleton.h"

namespace quicx {

class SSLConnection {
public:
    SSLConnection();
    ~SSLConnection();
    // init ssl connection
    bool Init(uint64_t sock, bool is_server = true);

private:
    static int32_t SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);

private:
    SSL *_ssl;
};

}

#endif