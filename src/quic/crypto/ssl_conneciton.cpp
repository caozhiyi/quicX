#include <cstring>
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

SSLConnection::SSLConnection(std::shared_ptr<TlsHandlerInterface> handler):
    _ssl(nullptr),
    _handler(handler) {

}

SSLConnection::~SSLConnection() {
    if (_ssl) {
        SSL_free(_ssl);
    }
}

bool SSLConnection::Init(bool is_server) {
    auto ssl_ctx = Singleton<SSLCtx>::Instance().GetSSLCtx();
    if (ssl_ctx == nullptr) {
        LOG_ERROR("empty ssl ctx.");
        return false;
    }
    
    _ssl = SSL_new(ssl_ctx);
    if (_ssl == nullptr) {
        LOG_ERROR("create ssl failed.");
        return false;
    }

    if (SSL_set_app_data(_ssl, this) == 0) {
        LOG_ERROR("SSL_set_app_data failed.");
        return false;
    }

    if (SSL_set_quic_method(_ssl, &__quic_method) == 0) {
        LOG_ERROR("SSL_set_quic_method failed.");
        return false;
    }


    if (is_server) {
        SSL_set_accept_state(_ssl);
    } else {
        SSL_set_connect_state(_ssl);
    }
    
    if (SSL_set_tlsext_host_name(_ssl, "localhost") == 0) {
        LOG_ERROR("SSL_set_tlsext_host_name failed.");
        return false;
    }

    SSL_set_quic_use_legacy_codepoint(_ssl, false);

    const char* alpn = "h3";
    size_t protos_len = strlen("h3") + 1;
    uint8_t *alpn_buf = new uint8_t[protos_len];
    alpn_buf[0] = 2;
    strncpy((char*)&alpn_buf[1], alpn, protos_len);
    alpn_buf[protos_len] = '\0';
    if (SSL_set_alpn_protos(_ssl, alpn_buf, protos_len) != 0) {
        LOG_ERROR("SSL_set_alpn_protos failed.");
        return false;
    }
    
    if (SSL_set_quic_transport_params(_ssl, alpn_buf, 5) == 0) {
        LOG_ERROR("SSL_set_alpn_protos failed.");
        return false;
    }

    return true;
}

bool SSLConnection::DoHandleShake() {
    int32_t ret = SSL_do_handshake(_ssl);

    if (ret <= 0) {
        int32_t ssl_err = SSL_get_error(_ssl, ret);
        if (ssl_err != SSL_ERROR_WANT_READ) {
            const char* err = SSL_error_description(ssl_err);
            LOG_ERROR("SSL_do_handshake failed. err:%s", err);
            return false;
        }
    }

    return true;    
}

bool SSLConnection::ProcessCryptoData(char* data, uint32_t len) {
    if (!SSL_provide_quic_data(_ssl, SSL_quic_read_level(_ssl), (uint8_t*)data, len)) {
        LOG_ERROR("SSL_provide_quic_data failed.");
        return false;
    }

    // todo handshake done, send handle done frame
    return true;
}

int32_t SSLConnection::SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    SSLConnection* conn = (SSLConnection*)SSL_get_app_data(ssl);
    
    conn->_handler->SetReadSecret(ssl, level, cipher, secret, secret_len);
    return 1;
}

int32_t SSLConnection::SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    SSLConnection* conn = (SSLConnection*)SSL_get_app_data(ssl);
    
    conn->_handler->SetWriteSecret(ssl, level, cipher, secret, secret_len);
    return 1;
}

int32_t SSLConnection::AddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t *data,
    size_t len) {
    SSLConnection* conn = (SSLConnection*)SSL_get_app_data(ssl);

    conn->_handler->WriteMessage(level, data, len);
    return 1;
}

int32_t SSLConnection::FlushFlight(SSL* ssl) {
    SSLConnection* conn = (SSLConnection*)SSL_get_app_data(ssl);

    conn->_handler->FlushFlight();
    return 1;
}

int32_t SSLConnection::SendAlert(SSL* ssl, ssl_encryption_level_t level, uint8_t alert) {
    SSLConnection* conn = (SSLConnection*)SSL_get_app_data(ssl);

    conn->_handler->SendAlert(level, alert);
    return 1;
}

}