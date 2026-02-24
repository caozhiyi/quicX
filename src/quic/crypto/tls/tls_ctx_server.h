#ifndef QUIC_CRYPTO_TLS_TLS_SERVER_CTX
#define QUIC_CRYPTO_TLS_TLS_SERVER_CTX

#include <string>
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
    // cipher_suites: TLS 1.3 cipher suites string (e.g., "TLS_CHACHA20_POLY1305_SHA256")
    virtual bool Init(const std::string& cert_file, const std::string& key_file, bool enable_early_data, 
        uint32_t session_ticket_timeout, const std::string& cipher_suites = "");
    virtual bool Init(const char* cert_pem, const char* key_pem, bool enable_early_data, 
        uint32_t session_ticket_timeout, const std::string& cipher_suites = "");

private:
    bool Init(bool enable_early_data, uint32_t session_ticket_timeout, const std::string& cipher_suites = "");
};

}
}

#endif