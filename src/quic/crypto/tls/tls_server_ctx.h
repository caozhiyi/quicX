#ifndef QUIC_CRYPTO_TLS_TLS_SERVER_CTX
#define QUIC_CRYPTO_TLS_TLS_SERVER_CTX

#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {

class TLSServerCtx:
    public TLSCtx {
public:
    TLSServerCtx();
    ~TLSServerCtx();
    // init ssl library and create global ssl ctx
    virtual bool Init(const std::string& cert_file, const std::string& key_file);

private:
    virtual bool SetCertificateAndKey(const std::string& cert_file, const std::string& key_file);
};

}

#endif