#ifndef QUIC_CRYPTO_TLS_TLS_SERVER_CONNECTION
#define QUIC_CRYPTO_TLS_TLS_SERVER_CONNECTION

#include <memory>
#include "quic/crypto/tls/tls_connection.h"

namespace quicx {
namespace quic {

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
    TLSServerConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler, TlsServerHandlerInterface* ser_handler);
    ~TLSServerConnection();
    // init ssl connection
    virtual bool Init();

    // do handshake
    virtual bool DoHandleShake();

    // add transport param
    virtual bool AddTransportParam(uint8_t* tp, uint32_t len);
private:
    static int SSLAlpnSelect(SSL* ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg);

private:
    TlsServerHandlerInterface* ser_handler_;
};

}
}

#endif