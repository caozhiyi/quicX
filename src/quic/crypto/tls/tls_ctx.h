#ifndef QUIC_CRYPTO_TLS_TLS_CTX
#define QUIC_CRYPTO_TLS_TLS_CTX

#include <string>
#include <cstdint>
#include "openssl/ssl.h"
#include "common/util/singleton.h"

namespace quicx {

class TLSCtx {
public:
    TLSCtx();
    virtual ~TLSCtx();
    // init ssl library and create global ssl ctx
    virtual bool Init();
    virtual void Destory();
    // get ssl ctx
    virtual SSL_CTX* GetSSLCtx() { return _ssl_ctx; }

protected:
    SSL_CTX *_ssl_ctx;
};


}

#endif