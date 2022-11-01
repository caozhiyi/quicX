#include "quic/crypto/tls_server_conneciton.h"

namespace quicx {

TLSServerConnection::TLSServerConnection(SSL_CTX *ctx, std::shared_ptr<TlsHandlerInterface> handler):
    TLSConnection(ctx, handler) {
}

TLSServerConnection::~TLSServerConnection() {

}

bool TLSServerConnection::Init() {
    if (!TLSConnection::Init()) {
        return false;
    }
    
    SSL_set_accept_state(_ssl);

    //SSL_set_quic_use_legacy_codepoint(_ssl, false);
    return true;
}

}