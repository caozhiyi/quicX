#include <vector>
#include <cstring>
#include "openssl/ssl.h"

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
    
    SSL_CTX_set_alpn_select_cb(ctx_->GetSSLCtx(), TLSServerConnection::SSLAlpnSelect, nullptr);
    
    SSL_set_accept_state(ssl_.get());

    // Advertise and enable server-side 0-RTT (early data) support via session tickets (OpenSSL >= 1.1.1).
    // BoringSSL does not use SSL_CTX_set_max_early_data; skip for BoringSSL to keep compatibility.
#if !defined(OPENSSL_IS_BORINGSSL)
#  if defined(OPENSSL_VERSION_NUMBER) && (OPENSSL_VERSION_NUMBER >= 0x10101000L)
    SSL_CTX_set_max_early_data(ctx_->GetSSLCtx(), 65536);
#  endif
#endif

    // For BoringSSL QUIC, the server must configure an early data context to allow 0-RTT on future resumptions.
    // Use a fixed context string. It must remain consistent across resumptions to accept early data.
#if defined(OPENSSL_IS_BORINGSSL)
    static const char kEarlyDataCtx[] = "quic-early-data";
    SSL_set_quic_early_data_context(ssl_.get(), reinterpret_cast<const uint8_t*>(kEarlyDataCtx), sizeof(kEarlyDataCtx) - 1);
#endif

    // Optional: early data context binding. If your BoringSSL exposes SSL_set_quic_early_data_context,
    // you may set a context here on both client and server to further constrain 0-RTT acceptance.
    // Not strictly required for enabling 0-RTT, so it's omitted for compatibility.

    return true;
}

int TLSServerConnection::SSLAlpnSelect(SSL* ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    TLSServerConnection* conn = (TLSServerConnection*)SSL_get_app_data(ssl);
    
    conn->ser_handler_->SSLAlpnSelect(out, outlen, in, inlen, arg);

    return 0;
}

}
}