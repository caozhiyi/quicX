#include "openssl/base.h"
#include "common/log/log.h"
#include "quic/crypto/ssl_ctx.h"
#include "quic/crypto/ssl_conneciton.h"

namespace quicx {

static const SSL_QUIC_METHOD __quic_method = {
    SSLConnection::SetReadSecret,
    SSLConnection::SetWriteSecret,
    SSLConnection::AddHandshakeData,
    SSLConnection::FlushFlight,
    SSLConnection::SendAlert,
};

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

    if (SSL_set_quic_method(_ssl, &__quic_method) == 0) {
        LOG_ERROR("SSL_set_quic_method failed.");
        return false;
    }
    
    return true;
}

bool SSLConnection::PutHandshakeData(char* data, uint32_t len) {
    if (!SSL_provide_quic_data(_ssl, SSL_quic_read_level(_ssl), (uint8_t*)data, len)) {
        LOG_ERROR("SSL_provide_quic_data failed.");
        return false;
    }

    int32_t ret = SSL_do_handshake(_ssl);

    if (ret <= 0) {
        int32_t ssl_err = SSL_get_error(_ssl, ret);
        if (ssl_err != SSL_ERROR_WANT_READ) {
            LOG_ERROR("SSL_do_handshake failed.");
            return false;
        }
    }

    if (SSL_in_init(_ssl)) {
        return true;
    }

    // todo handshake done, send handle done frame

    return true;
}

int32_t SSLConnection::SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    SSLConnection* conn = (SSLConnection*)SSL_get_ex_data(ssl, Singleton<SSLCtx>::Instance().GetSslConnectionIndex());
    
    if (!conn->MakeEncryptionSecret(false, level, cipher, secret, secret_len)) {
        LOG_ERROR("make encryption secret failed.");
        return 0;
    }

    // todo callback early data
    return 1;
}

int32_t SSLConnection::SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    SSLConnection* conn = (SSLConnection*)SSL_get_ex_data(ssl, Singleton<SSLCtx>::Instance().GetSslConnectionIndex());
    
    if (!conn->MakeEncryptionSecret(true, level, cipher, secret, secret_len)) {
        LOG_ERROR("make encryption secret failed.");
        return 0;
    }

    return 1;
}

int32_t SSLConnection::AddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t *data,
    size_t len) {
    SSLConnection* conn = (SSLConnection*)SSL_get_ex_data(ssl, Singleton<SSLCtx>::Instance().GetSslConnectionIndex());

    const uint8_t          *client_params;
    size_t                  client_params_len;
    SSL_get_peer_quic_transport_params(ssl, &client_params, &client_params_len);
    if (client_params_len == 0) {
        /* RFC 9001, 8.2.  QUIC Transport Parameters Extension */
        LOG_ERROR("missing transport parameters.");
        return 0;
    }

    // todo callback send data to peer
    return 1;
}

int32_t SSLConnection::FlushFlight(SSL* ssl) {
    // todo 
    return 1;
}

int32_t SSLConnection::SendAlert(SSL* ssl, ssl_encryption_level_t level, uint8_t alert) {
    // todo 
    return 1;
}

}