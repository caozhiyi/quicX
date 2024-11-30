#include <cstring>
#include "openssl/base.h"
#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/crypto/tls/tls_conneciton.h"

namespace quicx {
namespace quic {

static const SSL_QUIC_METHOD __quic_method = {
    TLSConnection::SetReadSecret,
    TLSConnection::SetWriteSecret,
    TLSConnection::AddHandshakeData,
    TLSConnection::FlushFlight,
    TLSConnection::SendAlert,
};

TLSConnection::TLSConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler):
    ssl_(nullptr),
    ctx_(ctx),
    handler_(handler) {
    
}

TLSConnection::~TLSConnection() {

}

bool TLSConnection::Init() {
    ssl_ = SSL_new(ctx_->GetSSLCtx());
    if (!ssl_) {
        common::LOG_ERROR("create ssl failed.");
        return false;
    }

    if (SSL_set_app_data(ssl_.get(), this) == 0) {
        common::LOG_ERROR("SSL_set_app_data failed.");
        return false;
    }

    if (SSL_set_quic_method(ssl_.get(), &__quic_method) == 0) {
        common::LOG_ERROR("SSL_set_quic_method failed.");
        return false;
    }
    
    return true;
}

bool TLSConnection::DoHandleShake() {
    int32_t ret = SSL_do_handshake(ssl_.get());

    if (ret <= 0) {
        int32_t ssl_err = SSL_get_error(ssl_.get(), ret);
        if (ssl_err != SSL_ERROR_WANT_READ) {
            const char* err = SSL_error_description(ssl_err);
            common::LOG_ERROR("SSL_do_handshake failed. err:%s", err);
        }
        return false;
    }

    return true;    
}

bool TLSConnection::ProcessCryptoData(uint8_t* data, uint32_t len) {
    if (!SSL_provide_quic_data(ssl_.get(), SSL_quic_read_level(ssl_.get()), data, len)) {
        common::LOG_ERROR("SSL_provide_quic_data failed.");
        return false;
    }

    // todo handshake done, send handle done frame
    return true;
}

bool TLSConnection::AddTransportParam(uint8_t* tp, uint32_t len) {
    if (SSL_set_quic_transport_params(ssl_.get(), tp, len) == 0) {
        common::LOG_ERROR("SSL_set_quic_transport_params failed.");
        return false;
    }
    
    return true;
}

EncryptionLevel TLSConnection::GetLevel() {
    return AdapterEncryptionLevel(SSL_quic_read_level(ssl_.get()));
}

int32_t TLSConnection::SetReadSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
    const uint8_t *secret, size_t secret_len) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);
    
    TryGetTransportParam(ssl, level);
    conn->handler_->SetReadSecret(ssl, AdapterEncryptionLevel(level), cipher, secret, secret_len);
    return 1;
}

int32_t TLSConnection::SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);
    
    TryGetTransportParam(ssl, level);
    conn->handler_->SetWriteSecret(ssl, AdapterEncryptionLevel(level), cipher, secret, secret_len);
    return 1;
}

int32_t TLSConnection::AddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t *data,
    size_t len) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);

    conn->handler_->WriteMessage(AdapterEncryptionLevel(level), data, len);
    return 1;
}

int32_t TLSConnection::FlushFlight(SSL* ssl) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);

    conn->handler_->FlushFlight();
    return 1;
}

int32_t TLSConnection::SendAlert(SSL* ssl, ssl_encryption_level_t level, uint8_t alert) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);

    conn->handler_->SendAlert(AdapterEncryptionLevel(level), alert);
    return 1;
}

void TLSConnection::TryGetTransportParam(SSL* ssl, ssl_encryption_level_t level) {
    const uint8_t *peer_tp;
    size_t tp_len = 0;
    SSL_get_peer_quic_transport_params(ssl, &peer_tp, &tp_len);
    if (tp_len == 0) {
        return;
    }

    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);
    conn->handler_->OnTransportParams(AdapterEncryptionLevel(level), peer_tp, tp_len);
}

EncryptionLevel TLSConnection::AdapterEncryptionLevel(ssl_encryption_level_t level) {
    switch (level)
    {
    case ssl_encryption_initial: return EL_INITIAL;
    case ssl_encryption_early_data: return EL_EARLY_DATA;
    case ssl_encryption_handshake: return EL_HANDSHAKE;
    case ssl_encryption_application: return EL_APPLICATION;
    default:
        common::LOG_ERROR("unknow encryption level. level:%d", level);
        abort();
    }
}

}
}