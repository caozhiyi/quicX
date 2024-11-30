#include "openssl/conf.h"

#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

TLSCtx::TLSCtx():
    ssl_ctx_(nullptr) {

}

TLSCtx::~TLSCtx() {

}

bool TLSCtx::Init() {
    ssl_ctx_ = SSLCtxPtr(SSL_CTX_new(TLS_method()));
    if (!ssl_ctx_) {
        common::LOG_ERROR("create ssl ctx failed");
        return false;
    }

    SSL_CTX_set_min_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);

    return true;
}

}
}