#include <gtest/gtest.h>
#include "quic/crypto/ssl_ctx.h"
#include "quic/crypto/tls_client_conneciton.h"

class TestHandler:
    public quicx::TlsHandlerInterface {
    void SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {

    }

    void SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {

    }

    void WriteMessage(ssl_encryption_level_t level, const uint8_t *data,
        size_t len) {

    }

    void FlushFlight() {

    }

    void SendAlert(ssl_encryption_level_t level, uint8_t alert) {

    }
};

TEST(crypto_ssl_connection_utest, test1) {
    quicx::SSLCtx ctx;
    ctx.Init();

    std::shared_ptr<TestHandler> handler = std::make_shared<TestHandler>();
    quicx::TLSClientConnection ssl_conn = quicx::TLSClientConnection(ctx.GetSSLCtx(), handler);
    ssl_conn.Init();

    const char* alpn = "h3";
    ssl_conn.AddAlpn((uint8_t*)alpn, 2);
    ssl_conn.AddTransportParam((uint8_t*)alpn, 2);
    ssl_conn.DoHandleShake();
}