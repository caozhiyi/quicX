#include <cstring>
#include "common/log/log.h"
#include "quic/crypto/tls_client_conneciton.h"

namespace quicx {

TLSClientConnection::TLSClientConnection(SSL_CTX *ctx, std::shared_ptr<TlsHandlerInterface> handler):
    TLSConnection(ctx, handler) {

}

TLSClientConnection::~TLSClientConnection() {

}

bool TLSClientConnection::Init() {
    if (!TLSConnection::Init()) {
        return false;
    }
    
    SSL_set_connect_state(_ssl);

    SSL_CTX_set_session_cache_mode(_ctx, 
        SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);

    
    return true;
}

bool TLSClientConnection::DoHandleShake() {
    return TLSConnection::DoHandleShake();
}

bool TLSClientConnection::AddAlpn(uint8_t* alpn, uint32_t len) {
    size_t protos_len = len + 1;
    uint8_t *alpn_buf = new uint8_t[protos_len];
    alpn_buf[0] = len;
    memcpy(&alpn_buf[1], alpn, protos_len);
    alpn_buf[protos_len] = '\0';
    if (SSL_set_alpn_protos(_ssl, alpn_buf, protos_len) != 0) {
        LOG_ERROR("SSL_set_alpn_protos failed.");
        return false;
    }

    return true;
}

}
