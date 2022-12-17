#include <iostream>
#include <gtest/gtest.h>
#include "quic/crypto/ssl_ctx.h"
#include "quic/crypto/tls_client_conneciton.h"
#include "quic/crypto/tls_server_conneciton.h"

class TestHandler:
    public quicx::TlsHandlerInterface {
public:
    TestHandler(): _len(0) {
        memset(_buf, 0, sizeof(_buf));
    }
    ~TestHandler() {}

    void getCryptoData(char* &data, uint32_t& len) {
        data = _buf;
        len = _len;
    }

    void SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
        
    }

    void SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {

    }

    void WriteMessage(ssl_encryption_level_t level, const uint8_t *data,
        size_t len) {
        memcpy(_buf, (void*)data, len);
        _len = len;
    }

    void FlushFlight() {

    }

    void SendAlert(ssl_encryption_level_t level, uint8_t alert) {

    }

private:
    char _buf[1024];
    int _len;
    bool _is_server;
};

class TestServerHandler:
    public quicx::TlsServerHandlerInterface {

    void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
        const unsigned char *in, unsigned int inlen, void *arg) {
        std::cout << std::string((char*)out, inlen) << std::endl;

        static const unsigned char* alpn = (const unsigned char*)"h3";
        static uint8_t len = 3;

        if (SSL_select_next_proto((unsigned char **)out, outlen, alpn, len, in, inlen) != OPENSSL_NPN_NEGOTIATED)  {
            return;
        }
    }
};

TEST(crypto_ssl_connection_utest, test1) {
    quicx::SSLCtx ctx;
    ctx.Init();

    std::shared_ptr<TestHandler> cli_handler = std::make_shared<TestHandler>();
    quicx::TLSClientConnection cli_conn = quicx::TLSClientConnection(ctx.GetSSLCtx(), cli_handler);
    cli_conn.Init();

    const char* alpn = "h3";
    cli_conn.AddAlpn((uint8_t*)alpn, 2);
    cli_conn.AddTransportParam((uint8_t*)alpn, 2);
    cli_conn.DoHandleShake();

    std::shared_ptr<TestHandler> ser_handler = std::make_shared<TestHandler>();
    std::shared_ptr<TestServerHandler> ser_alpn_handler = std::make_shared<TestServerHandler>();
    quicx::TLSServerConnection ser_conn = quicx::TLSServerConnection(ctx.GetSSLCtx(), ser_handler, ser_alpn_handler);
    ser_conn.Init("server.key", "server.crt");

    char* data;
    u_int32_t len = 0;
    cli_handler->getCryptoData(data, len);
    ser_conn.ProcessCryptoData(data, len);
    ser_conn.DoHandleShake();

    ser_handler->getCryptoData(data, len);
    cli_conn.ProcessCryptoData(data, len);
    cli_conn.DoHandleShake();
}