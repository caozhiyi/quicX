#ifndef QUIC_CRYPTO_TLS_TLS_CONNECTION
#define QUIC_CRYPTO_TLS_TLS_CONNECTION

#include <memory>
#include <string>
#include <cstdint>
#include "openssl/ssl.h"
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

class TLSConnection {
public:
    TLSConnection(SSL_CTX *ctx, std::shared_ptr<TlsHandlerInterface> handler);
    ~TLSConnection();
    // init ssl connection
    virtual bool Init();

    // do handshake
    virtual bool DoHandleShake();

    // add crypto data
    virtual bool ProcessCryptoData(char* data, uint32_t len);

    // add transport param
    virtual bool AddTransportParam(uint8_t* tp, uint32_t len);

    ssl_encryption_level_t GetLevel();

public:
    static int32_t SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);
    static int32_t SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);
    static int32_t AddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t *data,
        size_t len);
    static int32_t FlushFlight(SSL* ssl);
    static int32_t SendAlert(SSL* ssl, ssl_encryption_level_t level, uint8_t alert);

protected:
    SSL *_ssl;
    SSL_CTX *_ctx;
    std::shared_ptr<TlsHandlerInterface> _handler;
};

}

#endif