#ifndef QUIC_CRYPTO_TLS_TLS_CLIENT_CTX
#define QUIC_CRYPTO_TLS_TLS_CLIENT_CTX

#include <string>
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

class TLSClientCtx:
    public TLSCtx {
public:
    TLSClientCtx();
    ~TLSClientCtx();
    // init ssl library and create global ssl ctx
    // cipher_suites: TLS 1.3 cipher suites string (e.g., "TLS_CHACHA20_POLY1305_SHA256")
    // verify_peer: if true (default), enable peer certificate verification
    // ca_file: CA certificate file path for verification, empty uses system defaults
    virtual bool Init(bool enable_early_data, const std::string& cipher_suites = "",
                      bool verify_peer = true, const std::string& ca_file = "");
};

}
}

#endif