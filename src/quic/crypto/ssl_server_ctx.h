#ifndef QUIC_CRYPTO_SSL_SERVER_CTX
#define QUIC_CRYPTO_SSL_SERVER_CTX

#include "quic/crypto/ssl_ctx.h"

namespace quicx {

class SSLServerCtx:
    public SSLCtx {
public:
    SSLServerCtx();
    ~SSLServerCtx();
    // init ssl library and create global ssl ctx
    virtual bool Init();

    virtual bool SetCertificateAndKey(const std::string& cert_file, const std::string& key_file);
};

}

#endif