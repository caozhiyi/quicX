#ifndef QUIC_CRYPTO_TLS_SERVER_CONNECTION
#define QUIC_CRYPTO_TLS_SERVER_CONNECTION

#include <memory>
#include "quic/crypto/tls_conneciton.h"

namespace quicx {

class TLSServerConnection:
    public TLSConnection {
public:
    TLSServerConnection(SSL_CTX *ctx, std::shared_ptr<TlsHandlerInterface> handler);
    ~TLSServerConnection();
    // init ssl connection
    virtual bool Init();
};

}

#endif