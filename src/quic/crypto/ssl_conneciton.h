#ifndef QUIC_CRYPTO_SSL_CONNECTION
#define QUIC_CRYPTO_SSL_CONNECTION

#include <string>
#include <cstdint>
#include "openssl/ssl.h"
#include "quic/crypto/protector.h"
#include "common/util/singleton.h"

namespace quicx {

class TlsHandlerInterface {
public:
    TlsHandlerInterface() {}
    virtual ~TlsHandlerInterface() {}

    virtual void SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) = 0;

    virtual void SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) = 0;

    virtual void WriteMessage(ssl_encryption_level_t level, const uint8_t *data,
        size_t len) = 0;

    virtual void FlushFlight() = 0;
    virtual void SendAlert(ssl_encryption_level_t level, uint8_t alert) = 0;   
};

class SSLConnection:
    public Protector {
public:
    SSLConnection(std::shared_ptr<TlsHandlerInterface> handler);
    ~SSLConnection();
    // init ssl connection
    bool Init(bool is_server = true);

    // do handshake
    bool DoHandleShake();

    // add crypto data
    bool ProcessCryptoData(char* data, uint32_t len);

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
    std::shared_ptr<TlsHandlerInterface> _handler;
};

}

#endif