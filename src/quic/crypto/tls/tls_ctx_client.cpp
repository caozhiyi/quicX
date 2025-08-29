#include "quic/crypto/tls/tls_ctx_client.h"

namespace quicx {
namespace quic {

TLSClientCtx::TLSClientCtx() {

}

TLSClientCtx::~TLSClientCtx() {
    
}

bool TLSClientCtx::Init(bool enable_early_data) {
    if (!TLSCtx::Init(enable_early_data)) {
        return false;
    }

    // set session cache mode, enables session caching for both client and server.
    SSL_CTX_set_session_cache_mode(ssl_ctx_.get(), SSL_SESS_CACHE_BOTH);
    return true;
}

}
}