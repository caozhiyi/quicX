#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {
namespace quic {

TLSClientCtx::TLSClientCtx() {}

TLSClientCtx::~TLSClientCtx() {}

bool TLSClientCtx::Init(bool enable_early_data, const std::string& cipher_suites) {
    if (!TLSCtx::Init(enable_early_data, cipher_suites)) {
        return false;
    }

    // Disable certificate verification for local testing with self-signed certificates
    // In production, you should enable verification and provide proper CA certificates
    SSL_CTX_set_verify(ssl_ctx_.get(), SSL_VERIFY_NONE, nullptr);  // TODO configuralbe

    // set session cache mode, enables session caching for both client and server.
    SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_BOTH);
    return true;
}

}  // namespace quic
}  // namespace quicx