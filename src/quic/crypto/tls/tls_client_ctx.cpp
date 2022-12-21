#include "common/log/log.h"
#include "quic/crypto/tls/tls_client_ctx.h"

namespace quicx {

TLSClientCtx::TLSClientCtx() {

}

TLSClientCtx::~TLSClientCtx() {
    
}

bool TLSClientCtx::Init() {
    if (!TLSCtx::Init()) {
        return false;
    }

    // set client config 
    SSL_CTX_set_session_cache_mode(_ssl_ctx, 
        SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);

    return true;
}

}