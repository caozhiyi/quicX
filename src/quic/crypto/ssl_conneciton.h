#ifndef QUIC_CRYPTO_SSL_CONNECTION
#define QUIC_CRYPTO_SSL_CONNECTION

#include <string>
#include <cstdint>
#include "openssl/ssl.h"
#include "quic/crypto/protector.h"
#include "common/util/singleton.h"

namespace quicx {

class SSLConnection {
public:
    SSLConnection();
    ~SSLConnection();
    // init ssl connection
    bool Init(uint64_t sock, bool is_server = true);
    // add crypto data
    bool PutHandshakeData(char* data, uint32_t len);
    // encrypt
    // bool Encrypt();

public:
    static int32_t SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);
    static int32_t SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);
    static int32_t AddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t *data,
        size_t len);
    static int32_t FlushFlight(SSL* ssl);
    static int32_t SendAlert(SSL* ssl, ssl_encryption_level_t level, uint8_t alert);
private:
    SSL *_ssl;
    Protector _protector;
};

}

#endif