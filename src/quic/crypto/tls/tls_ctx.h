#ifndef QUIC_CRYPTO_TLS_TLS_CTX
#define QUIC_CRYPTO_TLS_TLS_CTX

#include <cstdio>
#include <string>
#include <openssl/ssl.h>
#include "quic/crypto/tls/type.h"

namespace quicx {
namespace quic {

class TLSCtx {
public:
    TLSCtx();
    virtual ~TLSCtx();
    // init ssl library and create global ssl ctx
    // cipher_suites: TLS 1.3 cipher suites string (e.g., "TLS_CHACHA20_POLY1305_SHA256")
    virtual bool Init(bool enable_early_data, const std::string& cipher_suites = "");
    // get ssl ctx
    virtual SSL_CTX* GetSSLCtx() { return ssl_ctx_.get(); }
    // get enable early data
    virtual bool GetEnableEarlyData() { return enable_early_data_; }

    // Enable TLS key logging (SSLKEYLOGFILE) for debugging with Wireshark
    bool EnableKeyLog(const std::string& keylog_file);

protected:
    SSLCtxPtr ssl_ctx_;
    bool enable_early_data_;

private:
    static void KeyLogCallback(const SSL* ssl, const char* line);
    FILE* keylog_file_ = nullptr;
};


}
}

#endif