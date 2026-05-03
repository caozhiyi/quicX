#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {
namespace quic {

TLSClientCtx::TLSClientCtx() {}

TLSClientCtx::~TLSClientCtx() {}

bool TLSClientCtx::Init(bool enable_early_data, const std::string& cipher_suites,
                         bool verify_peer, const std::string& ca_file) {
    if (!TLSCtx::Init(enable_early_data, cipher_suites)) {
        return false;
    }

    if (verify_peer) {
        SSL_CTX_set_verify(ssl_ctx_.get(), SSL_VERIFY_PEER, nullptr);
        if (!ca_file.empty()) {
            if (!SSL_CTX_load_verify_locations(ssl_ctx_.get(), ca_file.c_str(), nullptr)) {
                common::LOG_ERROR("failed to load CA file: %s", ca_file.c_str());
                return false;
            }
        }
        common::LOG_DEBUG("TLS peer certificate verification enabled");
    } else {
        SSL_CTX_set_verify(ssl_ctx_.get(), SSL_VERIFY_NONE, nullptr);
        common::LOG_WARN("TLS peer certificate verification DISABLED - not safe for production");
    }

    // set session cache mode, enables session caching for both client and server.
    SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_BOTH);
    return true;
}

}  // namespace quic
}  // namespace quicx