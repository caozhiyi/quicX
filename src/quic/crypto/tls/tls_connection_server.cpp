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

    if (!TLSConnection::Init()) {
        return false;
    }
    
    SSL_set_accept_state(ssl_.get());

    // Advertise and enable server-side 0-RTT (early data) support via session tickets.
    // The value indicates the max accepted early data size embedded in new tickets.
    SSL_CTX_set_max_early_data(ctx_->GetSSLCtx(), 65536);

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