#ifndef QUIC_CRYPTO_TLS_SERVER_CONNECTION
#define QUIC_CRYPTO_TLS_SERVER_CONNECTION

#include <memory>
#include "quic/crypto/tls_conneciton.h"

namespace quicx {

class TlsServerHandlerInterface {
public:
    TlsServerHandlerInterface() {}
    virtual ~TlsServerHandlerInterface() {}

    virtual void SSLAlpnSelect(const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) = 0;
};

class TLSServerConnection:
    public TLSConnection {
public:
    TLSServerConnection(SSL_CTX *ctx, std::shared_ptr<TlsHandlerInterface> handler, std::shared_ptr<TlsServerHandlerInterface> ser_handler);
    ~TLSServerConnection();
    // init ssl connection
    virtual bool Init();

private:
    static int SSLAlpnSelect(SSL* ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg);

private:
    std::shared_ptr<TlsServerHandlerInterface> _ser_handler;
};

}

#endif