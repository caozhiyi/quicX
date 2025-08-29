#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

TLSCtx::TLSCtx():
    ssl_ctx_(nullptr),
    enable_early_data_(false) {

}

TLSCtx::~TLSCtx() {

}

bool TLSCtx::Init(bool enable_early_data) {
    ssl_ctx_ = SSLCtxPtr(SSL_CTX_new(TLS_method()));
    if (!ssl_ctx_) {
        common::LOG_ERROR("create ssl ctx failed");
        return false;
    }

    SSL_CTX_set_min_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);

    if (enable_early_data) {
        // Enable early data on the context so NSTs allow 0-RTT on resumption
        SSL_CTX_set_early_data_enabled(ssl_ctx_.get(), 1);
        enable_early_data_ = true;
    }
    
    return true;
}

}
}