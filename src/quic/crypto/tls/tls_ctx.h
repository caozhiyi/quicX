#ifndef QUIC_CRYPTO_TLS_TLS_CTX
#define QUIC_CRYPTO_TLS_TLS_CTX

#include <string>
#include <cstdint>
#include <openssl/ssl.h>
#include "quic/crypto/tls/type.h"

namespace quicx {
namespace quic {

class TLSCtx {
public:
    TLSCtx();
    virtual ~TLSCtx();
    // init ssl library and create global ssl ctx
    virtual bool Init();
    // get ssl ctx
    virtual SSL_CTX* GetSSLCtx() { return ssl_ctx_.get(); }

protected:
    SSLCtxPtr ssl_ctx_;
};


}
}

#endif