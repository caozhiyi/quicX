#ifndef QUIC_CRYPTO_TLS_TLS_CLIENT_CONNECTION
#define QUIC_CRYPTO_TLS_TLS_CLIENT_CONNECTION

#include "quic/crypto/tls/tls_conneciton.h"

namespace quicx {
namespace quic {

class TLSClientConnection:
    public TLSConnection {
public:
    TLSClientConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler);
    ~TLSClientConnection();
    // init ssl connection
    virtual bool Init();

    // do handshake
    virtual bool DoHandleShake();

    // add alpn
    virtual bool AddAlpn(uint8_t* alpn, uint32_t len); 
};

}
}

#endif