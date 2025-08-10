#ifndef QUIC_CRYPTO_TLS_TLS_CONNECTION
#define QUIC_CRYPTO_TLS_TLS_CONNECTION

#include <memory>
#include <string>
#include <cstdint>
#include "quic/crypto/tls/type.h"
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

class TlsHandlerInterface {
public:
    TlsHandlerInterface() {}
    virtual ~TlsHandlerInterface() {}

    virtual void SetReadSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) = 0;

    virtual void SetWriteSecret(SSL* ssl, EncryptionLevel level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) = 0;

    virtual void WriteMessage(EncryptionLevel level, const uint8_t *data,
        size_t len) = 0;

    virtual void FlushFlight() = 0;
    virtual void SendAlert(EncryptionLevel level, uint8_t alert) = 0;
    virtual void OnTransportParams(EncryptionLevel level, const uint8_t* tp, size_t tp_len) = 0;
};

class TLSConnection {
public:
    TLSConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler);
    ~TLSConnection();
    // init ssl connection
    virtual bool Init();

    // do handshake
    virtual bool DoHandleShake();

    // add crypto data
    virtual bool ProcessCryptoData(uint8_t* data, uint32_t len);

    // add transport param
    virtual bool AddTransportParam(uint8_t* tp, uint32_t len);

    EncryptionLevel GetLevel();

public:
    static int32_t SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);
    static int32_t SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len);
    static int32_t AddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t *data,
        size_t len);
    static int32_t FlushFlight(SSL* ssl);
    static int32_t SendAlert(SSL* ssl, ssl_encryption_level_t level, uint8_t alert);
    static void TryGetTransportParam(SSL* ssl, ssl_encryption_level_t level);

    static EncryptionLevel AdapterEncryptionLevel(ssl_encryption_level_t level);
protected:
    SSLPtr ssl_;
    std::shared_ptr<TLSCtx> ctx_;
    TlsHandlerInterface* handler_;
public:
    // expose SSL pointer for session get/save by upper layer
    SSL* GetSSL() { return ssl_.get(); }
};

}
}

#endif