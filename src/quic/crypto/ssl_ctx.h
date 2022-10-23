#ifndef QUIC_CRYPTO_SSL_CTX
#define QUIC_CRYPTO_SSL_CTX

#include <string>
#include <cstdint>
#include "openssl/ssl.h"
#include "common/util/singleton.h"

namespace quicx {

class SSLCtx {
public:
    SSLCtx();
    virtual ~SSLCtx();
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