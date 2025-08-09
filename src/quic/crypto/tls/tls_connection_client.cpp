#include <cstring>
#include "common/log/log.h"
#include "quic/crypto/tls/tls_connection_client.h"

namespace quicx {
namespace quic {

TLSClientConnection::TLSClientConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler):
    TLSConnection(ctx, handler) {

}

TLSClientConnection::~TLSClientConnection() {

}

bool TLSClientConnection::Init() {
    if (!TLSConnection::Init()) {
        return false;
    }

    SSL_CTX_set_session_cache_mode(ctx_->GetSSLCtx(), SSL_SESS_CACHE_BOTH);

    SSL_set_connect_state(ssl_.get());
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
    if (SSL_set_alpn_protos(ssl_.get(), alpn_buf, protos_len) != 0) {
        common::LOG_ERROR("SSL_set_alpn_protos failed.");
        return false;
    }

    return true;
}

}
}
