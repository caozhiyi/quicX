#ifndef QUIC_CRYPTO_TLS_TLS_CLIENT_CTX
#define QUIC_CRYPTO_TLS_TLS_CLIENT_CTX

#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {

class TLSClientCtx:
    public TLSCtx {
public:
    TLSClientCtx();
    ~TLSClientCtx();
    // init ssl library and create global ssl ctx
    virtual bool Init();
};

}

#endif