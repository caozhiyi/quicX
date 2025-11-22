#include <openssl/ssl.h>

#include "common/log/log.h"
#include "quic/crypto/tls/tls_connection_server.h"

namespace quicx {
namespace quic {

TLSServerConnection::TLSServerConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler, TlsServerHandlerInterface* ser_handle):
    TLSConnection(ctx, handler),
    ser_handler_(ser_handle) {

}

TLSServerConnection::~TLSServerConnection() {

}

bool TLSServerConnection::Init() {
    if (!TLSConnection::Init()) {
        return false;
    }
    
    // set alpn select callback, this callback will be called when the client send alpn list.
    SSL_CTX_set_alpn_select_cb(ctx_->GetSSLCtx(), TLSServerConnection::SSLAlpnSelect, nullptr);
    
    // set accept state, this will start the handshake process.
    SSL_set_accept_state(ssl_.get());

    return true;
}

bool TLSServerConnection::DoHandleShake() {
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

bool TLSServerConnection::AddTransportParam(uint8_t* tp, uint32_t len) {
    if (!TLSConnection::AddTransportParam(tp, len)) {
        return false;
    }
    
    if (ctx_->GetEnableEarlyData()) {
        // For BoringSSL QUIC, the server must configure an early data context to allow 0-RTT on future resumptions.
        // Use a fixed context string. It must remain consistent across resumptions to accept early data.
        SSL_set_quic_early_data_context(ssl_.get(), tp, len);
    }
    return true;
}

int TLSServerConnection::SSLAlpnSelect(SSL* ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    TLSServerConnection* conn = (TLSServerConnection*)SSL_get_app_data(ssl);
    
    // Store original values to detect if handler set them
    const unsigned char *original_out = *out;
    unsigned char original_outlen = *outlen;
    
    conn->ser_handler_->SSLAlpnSelect(out, outlen, in, inlen, arg);

    // Check if the handler successfully selected an ALPN
    if (*out != original_out || *outlen != original_outlen) {
        // ALPN was selected
        return SSL_TLSEXT_ERR_OK;
    } else {
        // No ALPN match found
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
}

}
}