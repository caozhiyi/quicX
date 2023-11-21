#ifndef QUIC_CRYPTO_TLS_TLS_SERVER_CTX
#define QUIC_CRYPTO_TLS_TLS_SERVER_CTX

#include <openssl/ssl.h>
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

class TLSServerCtx:
    public TLSCtx {
public:
    TLSServerCtx();
    ~TLSServerCtx();
    // init ssl library and create global ssl ctx
    virtual bool Init(const std::string& cert_file, const std::string& key_file);
    virtual bool Init(X509* cert, EVP_PKEY* key);

private:
    bool Init();
};

}
}

#endif