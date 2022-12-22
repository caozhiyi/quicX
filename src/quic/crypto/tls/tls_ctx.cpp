#include "openssl/conf.h"

#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {

TLSCtx::TLSCtx():
    _ssl_ctx(nullptr) {

}

TLSCtx::~TLSCtx() {

}

bool TLSCtx::Init() {
    _ssl_ctx = SSLCtxPtr(SSL_CTX_new(TLS_method()));
    if (!_ssl_ctx) {
        LOG_ERROR("create ssl ctx failed");
        return false;
    }

    SSL_CTX_set_min_proto_version(_ssl_ctx.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(_ssl_ctx.get(), TLS1_3_VERSION);

    return true;
}

}