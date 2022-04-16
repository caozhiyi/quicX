#include "common/log/log.h"
#include "quic/crypto/ssl_ctx.h"
#include "quic/crypto/ssl_conneciton.h"

namespace quicx {

SSLConnection::SSLConnection():
    _ssl(nullptr) {

}

SSLConnection::~SSLConnection() {
    if (_ssl) {
        SSL_free(_ssl);
    }
}

bool SSLConnection::Init(uint64_t sock, bool is_server) {
    auto ssl_ctx = Singleton<SSLCtx>::Instance().GetSSLCtx();
    if (ssl_ctx) {
        LOG_ERROR("empty ssl ctx.");
        return false;
    }
    
    _ssl = SSL_new(ssl_ctx);
    if (_ssl != nullptr) {
        LOG_ERROR("create ssl failed.");
        return false;
    }

    if (SSL_set_fd(_ssl, sock) == 0) {
        LOG_ERROR("SSL_set_fd failed.");
        return false;
    }

    if (is_server) {
        SSL_set_accept_state(_ssl);
    } else {
        SSL_set_connect_state(_ssl);
    }
    
    if (SSL_set_ex_data(_ssl, Singleton<SSLCtx>::Instance().GetSslConnectionIndex(), this) == 0) {
        LOG_ERROR("SSL_set_ex_data failed.");
        return false;
    }
    
    return true;
}

int32_t SSLConnection::SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    SSLConnection* conn = (SSLConnection*)SSL_get_ex_data(ssl, Singleton<SSLCtx>::Instance().GetSslConnectionIndex());
    
}

}