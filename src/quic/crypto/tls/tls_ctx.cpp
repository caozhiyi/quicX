#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx.h"

namespace quicx {
namespace quic {

TLSCtx::TLSCtx():
    ssl_ctx_(nullptr),
    enable_early_data_(false),
    keylog_file_(nullptr) {

}

TLSCtx::~TLSCtx() {
    if (keylog_file_) {
        fclose(keylog_file_);
        keylog_file_ = nullptr;
    }
}

bool TLSCtx::Init(bool enable_early_data, const std::string& cipher_suites) {
    ssl_ctx_ = SSLCtxPtr(SSL_CTX_new(TLS_method()));
    if (!ssl_ctx_) {
        common::LOG_ERROR("create ssl ctx failed");
        return false;
    }

    // Store this pointer as app data so keylog callback can access it
    SSL_CTX_set_app_data(ssl_ctx_.get(), this);

    SSL_CTX_set_min_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);

    // Note: BoringSSL does not support configuring TLS 1.3 cipher suites via
    // SSL_CTX_set_cipher_list. TLS 1.3 ciphers have a built-in preference order.
    // BoringSSL automatically selects the best cipher based on:
    // - Hardware support (AES-NI for AES-GCM, or ChaCha20 otherwise)
    // - Security strength
    // 
    // Supported TLS 1.3 ciphers:
    // - TLS_AES_128_GCM_SHA256 (default)
    // - TLS_AES_256_GCM_SHA384
    // - TLS_CHACHA20_POLY1305_SHA256
    //
    // For interoperability testing, ChaCha20 can be tested by running on
    // platforms without AES-NI hardware support.
    if (!cipher_suites.empty()) {
        common::LOG_INFO("TLS cipher suites requested: %s (note: BoringSSL uses automatic TLS 1.3 cipher selection)", 
            cipher_suites.c_str());
    }

    if (enable_early_data) {
        // Enable early data on the context so NSTs allow 0-RTT on resumption
        SSL_CTX_set_early_data_enabled(ssl_ctx_.get(), 1);
        enable_early_data_ = true;
    }
    
    return true;
}

bool TLSCtx::EnableKeyLog(const std::string& keylog_file) {
    if (keylog_file.empty()) {
        return false;
    }

    keylog_file_ = fopen(keylog_file.c_str(), "a");
    if (!keylog_file_) {
        common::LOG_ERROR("failed to open keylog file: %s", keylog_file.c_str());
        return false;
    }

    SSL_CTX_set_keylog_callback(ssl_ctx_.get(), TLSCtx::KeyLogCallback);
    common::LOG_INFO("TLS key logging enabled: %s", keylog_file.c_str());
    return true;
}

void TLSCtx::KeyLogCallback(const SSL* ssl, const char* line) {
    // Retrieve the TLSCtx via SSL_CTX app data
    SSL_CTX* ctx = SSL_get_SSL_CTX(ssl);
    if (!ctx) {
        return;
    }

    void* app_data = SSL_CTX_get_app_data(ctx);
    if (!app_data) {
        return;
    }

    TLSCtx* tls_ctx = static_cast<TLSCtx*>(app_data);
    if (tls_ctx->keylog_file_) {
        fprintf(tls_ctx->keylog_file_, "%s\n", line);
        fflush(tls_ctx->keylog_file_);
    }
}

}
}