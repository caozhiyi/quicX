#include <openssl/err.h>
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
            common::LOG_ERROR("SSL_do_handshake failed. ssl_err:%d, desc:%s", ssl_err, err ? err : "null");
            unsigned long err_code;
            while ((err_code = ERR_get_error()) != 0) {
                char output[256];
                ERR_error_string_n(err_code, output, sizeof(output));
                common::LOG_ERROR("OpenSSL Error: %s", output);
            }
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
        // IMPORTANT: The context MUST remain identical across connections for BoringSSL to accept 0-RTT.
        // Previously we passed the full transport parameters (tp, len) here, but those include
        // original_destination_connection_id_ which is unique per connection, causing BoringSSL
        // to reject 0-RTT on resumed connections (context mismatch).
        // Fix: Use a fixed context string that stays consistent across all connections.
        static const uint8_t kEarlyDataContext[] = "quicx-0rtt-v1";
        SSL_set_quic_early_data_context(ssl_.get(), kEarlyDataContext, sizeof(kEarlyDataContext) - 1);
    }
    return true;
}

int TLSServerConnection::SSLAlpnSelect(SSL* ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    TLSServerConnection* conn = (TLSServerConnection*)SSL_get_app_data(ssl);
    
    // Initialize to known sentinel values before calling the handler.
    // The previous code compared against *out/*outlen's original values,
    // but those are uninitialized from BoringSSL, causing undefined behavior.
    *out = nullptr;
    *outlen = 0;
    
    conn->ser_handler_->SSLAlpnSelect(out, outlen, in, inlen, arg);

    // Check if the handler successfully selected an ALPN
    if (*out != nullptr && *outlen > 0) {
        // ALPN was selected
        return SSL_TLSEXT_ERR_OK;
    } else {
        // No ALPN match found
        return SSL_TLSEXT_ERR_ALERT_FATAL;
    }
}

}
}