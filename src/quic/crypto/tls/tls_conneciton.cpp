#include <cstring>
#include "openssl/base.h"
#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/crypto/tls/tls_conneciton.h"

namespace quicx {

static const SSL_QUIC_METHOD __quic_method = {
    TLSConnection::SetReadSecret,
    TLSConnection::SetWriteSecret,
    TLSConnection::AddHandshakeData,
    TLSConnection::FlushFlight,
    TLSConnection::SendAlert,
};

TLSConnection::TLSConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler):
    _ssl(nullptr),
    _ctx(ctx),
    _handler(handler) {
    
}

TLSConnection::~TLSConnection() {

}

bool TLSConnection::Init() {
    _ssl = SSL_new(_ctx->GetSSLCtx());
    if (!_ssl) {
        LOG_ERROR("create ssl failed.");
        return false;
    }

    if (SSL_set_app_data(_ssl.get(), this) == 0) {
        LOG_ERROR("SSL_set_app_data failed.");
        return false;
    }

    if (SSL_set_quic_method(_ssl.get(), &__quic_method) == 0) {
        LOG_ERROR("SSL_set_quic_method failed.");
        return false;
    }
    
    return true;
}

bool TLSConnection::DoHandleShake() {
    int32_t ret = SSL_do_handshake(_ssl.get());

    if (ret <= 0) {
        int32_t ssl_err = SSL_get_error(_ssl.get(), ret);
        if (ssl_err != SSL_ERROR_WANT_READ) {
            const char* err = SSL_error_description(ssl_err);
            LOG_ERROR("SSL_do_handshake failed. err:%s", err);
        }
        return false;
    }

    return true;    
}

bool TLSConnection::ProcessCryptoData(uint8_t* data, uint32_t len) {
    if (!SSL_provide_quic_data(_ssl.get(), SSL_quic_read_level(_ssl.get()), data, len)) {
        LOG_ERROR("SSL_provide_quic_data failed.");
        return false;
    }

    // todo handshake done, send handle done frame
    return true;
}

bool TLSConnection::AddTransportParam(uint8_t* tp, uint32_t len) {
    if (SSL_set_quic_transport_params(_ssl.get(), tp, len) == 0) {
        LOG_ERROR("SSL_set_quic_transport_params failed.");
        return false;
    }
    
    return true;
}

EncryptionLevel TLSConnection::GetLevel() {
    return AdapterEncryptionLevel(SSL_quic_read_level(_ssl.get()));
}

int32_t TLSConnection::SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);
    
    conn->_handler->SetReadSecret(ssl, AdapterEncryptionLevel(level), cipher, secret, secret_len);
    return 1;
}

int32_t TLSConnection::SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);
    
    conn->_handler->SetWriteSecret(ssl, AdapterEncryptionLevel(level), cipher, secret, secret_len);
    return 1;
}

int32_t TLSConnection::AddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t *data,
    size_t len) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);

    conn->_handler->WriteMessage(AdapterEncryptionLevel(level), data, len);
    return 1;
}

int32_t TLSConnection::FlushFlight(SSL* ssl) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);

    conn->_handler->FlushFlight();
    return 1;
}

int32_t TLSConnection::SendAlert(SSL* ssl, ssl_encryption_level_t level, uint8_t alert) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);

    conn->_handler->SendAlert(AdapterEncryptionLevel(level), alert);
    return 1;
}

EncryptionLevel TLSConnection::AdapterEncryptionLevel(ssl_encryption_level_t level) {
    switch (level)
    {
    case ssl_encryption_initial: return EL_INITIAL;
    case ssl_encryption_early_data: return EL_EARLY_DATA;
    case ssl_encryption_handshake: return EL_HANDSHAKE;
    case ssl_encryption_application: return EL_APPLICATION;
    default:
        LOG_ERROR("unknow encryption level. level:%d", level);
        abort();
    }
}

}