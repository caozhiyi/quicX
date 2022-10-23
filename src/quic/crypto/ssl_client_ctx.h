#ifndef QUIC_CRYPTO_SSL_CLIENT_CTX
#define QUIC_CRYPTO_SSL_CLIENT_CTX

#include "quic/crypto/ssl_ctx.h"

namespace quicx {

class SSLClientCtx:
    public SSLCtx {
public:
    SSLClientCtx();
    ~SSLClientCtx();
    // init ssl library and create global ssl ctx
    virtual bool Init();
};

}

#endif