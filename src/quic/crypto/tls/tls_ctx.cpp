#include "quic/crypto/tls/tls_ctx.h"
#include "common/log/log.h"

#include <cctype>

namespace quicx {
namespace quic {

TLSCtx::TLSCtx():
    ssl_ctx_(nullptr),
    enable_early_data_(false),
    keylog_file_(nullptr) {}

TLSCtx::~TLSCtx() {
    if (keylog_file_) {
        fclose(keylog_file_);
        keylog_file_ = nullptr;
    }
}

bool TLSCtx::Init(bool enable_early_data, const std::string& cipher_suites) {
    ssl_ctx_ = SSLCtxPtr(SSL_CTX_new(TLS_method()));
    if (!ssl_ctx_) {
        LOG_ERROR("create ssl ctx failed");
        return false;
    }

    // Store this pointer as app data so keylog callback can access it
    SSL_CTX_set_app_data(ssl_ctx_.get(), this);

    SSL_CTX_set_min_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(ssl_ctx_.get(), TLS1_3_VERSION);

    // NOTE on TLS 1.3 cipher-suite selection in this vendored BoringSSL:
    // - SSL_CTX_set_cipher_list() only configures the TLS 1.2 CIPHER_ORDER list
    //   (see ssl_create_cipher_list in ssl_cipher.cc); the three TLS 1.3 suites
    //   are tracked separately (NumTLS13Ciphers) and auto-negotiated, so passing
    //   a TLS 1.3 name here yields NO_CIPHER_MATCH and breaks endpoint startup.
    // - SSL_CTX_set_ciphersuites() is NOT exported by this version.
    // - TLS 1.3 suite selection is instead driven by AES hardware availability:
    //   with AES-NI it prefers AES-GCM, without it prefers ChaCha20
    //   (AesHwCipherScorer in s3_both.cc, choose_tls13_cipher in tls13_server.cc).
    // To honor a request for the ChaCha20 suite we emulate a "no AES hardware"
    // environment via the public testing override, so BoringSSL prefers ChaCha20
    // (0x1303) during TLS 1.3 negotiation. Both client and server inherit this
    // override (ssl_lib.cc copies it into the SSL config).
    if (!cipher_suites.empty()) {
        std::string cs = cipher_suites;
        for (char& c : cs) {
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        }
        if (cs.find("chacha20") != std::string::npos) {
            bssl::SSL_CTX_set_aes_hw_override_for_testing(ssl_ctx_.get(), false);
            LOG_INFO("TLS 1.3: ChaCha20 requested -> preferring ChaCha20 "
                     "(BoringSSL AES-HW override = false)");
        } else {
            LOG_WARN("TLS 1.3 cipher override '%s' not directly enforceable by "
                     "this BoringSSL build (only ChaCha20 preference is supported "
                     "via the AES-HW override); default selection in effect",
                     cipher_suites.c_str());
        }
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
        LOG_ERROR("failed to open keylog file: %s", keylog_file.c_str());
        return false;
    }

    SSL_CTX_set_keylog_callback(ssl_ctx_.get(), TLSCtx::KeyLogCallback);
    LOG_INFO("TLS key logging enabled: %s", keylog_file.c_str());
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

}  // namespace quic
}  // namespace quicx