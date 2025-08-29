#include <cstring>
#include "openssl/base.h"
#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx.h"
#include "quic/crypto/tls/tls_connection.h"

namespace quicx {
namespace quic {

static const SSL_QUIC_METHOD gQuicMethod = {
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

    if (SSL_set_quic_method(ssl_.get(), &gQuicMethod) == 0) {
        common::LOG_ERROR("SSL_set_quic_method failed.");
        return false;
    }
    
    return true;
}

bool TLSConnection::DoHandleShake() {
    int32_t ret = SSL_do_handshake(ssl_.get());

    if (ret <= 0) {
        int32_t ssl_err = SSL_get_error(ssl_.get(), ret);
        
        // Handle 0-RTT rejection according to RFC 9001
        if (ssl_err == SSL_ERROR_EARLY_DATA_REJECTED) {
            common::LOG_INFO("0-RTT data was rejected by server, resetting and continuing with full handshake");
            
            // Reset early data state and continue with full handshake
            SSL_reset_early_data_reject(ssl_.get());
            
            // Retry the handshake
            ret = SSL_do_handshake(ssl_.get());
            if (ret <= 0) {
                ssl_err = SSL_get_error(ssl_.get(), ret);
                if (ssl_err != SSL_ERROR_WANT_READ) {
                    const char* err = SSL_error_description(ssl_err);
                    common::LOG_ERROR("SSL_do_handshake failed after 0-RTT reset. err:%s", err);
                }
                return false;
            }
            return true;
        }
        
        if (ssl_err != SSL_ERROR_WANT_READ) {
            const char* err = SSL_error_description(ssl_err);
            common::LOG_ERROR("SSL_do_handshake failed. err:%s", err);
        }
        return false;
    }

    return true;    
}

bool TLSConnection::IsInEarlyData() const {
    return SSL_in_early_data(ssl_.get()) != 0;
}

bool TLSConnection::IsEarlyDataAccepted() const {
    return SSL_early_data_accepted(ssl_.get()) != 0;
}

int TLSConnection::GetEarlyDataReason() const {
    return static_cast<int>(SSL_get_early_data_reason(ssl_.get()));
}

const char* TLSConnection::GetEarlyDataReasonString() const {
    return SSL_early_data_reason_string(static_cast<ssl_early_data_reason_t>(GetEarlyDataReason()));
}

bool TLSConnection::ProcessCryptoData(uint8_t* data, uint32_t len) {
    auto level = SSL_quic_read_level(ssl_.get());
    common::LOG_DEBUG("process crypto data level: %d, len: %d", level, len);
    if (!SSL_provide_quic_data(ssl_.get(), level, data, len)) {
        common::LOG_ERROR("SSL_provide_quic_data failed.");
        return false;
    }
    // In QUIC/TLS 1.3, NewSessionTicket and other post-handshake messages arrive at application level.
    // BoringSSL requires the application to explicitly process them.
    if (level == ssl_encryption_application) {
        SSL_process_quic_post_handshake(ssl_.get());
    }
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

    common::LOG_DEBUG("set read secret level: %d, len: %d", AdapterEncryptionLevel(level), secret_len);

    TryGetTransportParam(ssl, level);
    conn->handler_->SetReadSecret(ssl, AdapterEncryptionLevel(level), cipher, secret, secret_len);
    return 1;
}

int32_t TLSConnection::SetWriteSecret(SSL* ssl, ssl_encryption_level_t level, const SSL_CIPHER *cipher,
        const uint8_t *secret, size_t secret_len) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);
    
    common::LOG_DEBUG("set write secret level: %d, len: %d", AdapterEncryptionLevel(level), secret_len);

    TryGetTransportParam(ssl, level);
    conn->handler_->SetWriteSecret(ssl, AdapterEncryptionLevel(level), cipher, secret, secret_len);
    return 1;
}

int32_t TLSConnection::AddHandshakeData(SSL* ssl, ssl_encryption_level_t level, const uint8_t *data,
    size_t len) {
    TLSConnection* conn = (TLSConnection*)SSL_get_app_data(ssl);
    common::LOG_DEBUG("add handshake data level: %d, len: %d", AdapterEncryptionLevel(level), len);
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
    case ssl_encryption_initial: return kInitial;
    case ssl_encryption_early_data: return kEarlyData;
    case ssl_encryption_handshake: return kHandshake;
    case ssl_encryption_application: return kApplication;
    default:
        common::LOG_ERROR("unknow encryption level. level:%d", level);
        abort();
    }
}

}
}