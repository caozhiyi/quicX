#ifndef QUIC_CRYPTO_TLS_TLS_CTX
#define QUIC_CRYPTO_TLS_TLS_CTX

#include <openssl/ssl.h>
#include "quic/crypto/tls/type.h"

namespace quicx {
namespace quic {

class TLSCtx {
public:
    TLSCtx();
    virtual ~TLSCtx();
    // init ssl library and create global ssl ctx
    virtual bool Init(bool enable_early_data);
    // get ssl ctx
    virtual SSL_CTX* GetSSLCtx() { return ssl_ctx_.get(); }
    // get enable early data
    virtual bool GetEnableEarlyData() { return enable_early_data_; }

protected:
    SSLCtxPtr ssl_ctx_;
    bool enable_early_data_;
};


}
}

#endif