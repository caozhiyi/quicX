#ifndef QUIC_CRYPTO_SSL_CTX
#define QUIC_CRYPTO_SSL_CTX

#include <string>
#include <cstdint>
#include "openssl/ssl.h"
#include "common/util/singleton.h"

namespace quicx {

class SSLCtx:
    public Singleton<SSLCtx> {
public:
    SSLCtx();
    ~SSLCtx();
    // init ssl library and create global ssl ctx
    bool Init();
    void Destory();
    // get ssl ctx
    SSL_CTX* GetSSLCtx() { return _ssl_ctx; }

private:
    SSL_CTX *_ssl_ctx;
};


}

#endif