#include "openssl/conf.h"

#include "common/log/log.h"
#include "quic/crypto/ssl_ctx.h"

namespace quicx {

SSLCtx::SSLCtx():
    _ssl_ctx(nullptr) {

}

SSLCtx::~SSLCtx() {

}

bool SSLCtx::Init() {
    _ssl_ctx = SSL_CTX_new(TLS_method());
    if (_ssl_ctx == nullptr) {
        LOG_ERROR("create ssl ctx failed");
        return false;
    }

    SSL_CTX_set_min_proto_version(_ssl_ctx, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(_ssl_ctx, TLS1_3_VERSION);

    // SSL_CTX_set_session_cache_mode(_ssl_ctx, 
    //     SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);

    return true;
}

void SSLCtx::Destory() {
    if (_ssl_ctx != nullptr) {
        SSL_CTX_free(_ssl_ctx);
    }
}

}