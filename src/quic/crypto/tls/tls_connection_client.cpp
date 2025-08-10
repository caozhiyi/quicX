#include <cstring>
#include "common/log/log.h"
#include "quic/crypto/tls/tls_connection_client.h"
#include "openssl/ssl.h"

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

    // 0-RTT will be enabled automatically on resumption when session ticket allows it.
    // If your BoringSSL has SSL_set_quic_early_data_context and you want extra binding,
    // you can add it here to match server policy.
#if defined(OPENSSL_IS_BORINGSSL)
    static const char kEarlyDataCtx[] = "quic-early-data";
    SSL_set_quic_early_data_context(ssl_.get(), reinterpret_cast<const uint8_t*>(kEarlyDataCtx), sizeof(kEarlyDataCtx) - 1);
    // Explicitly enable early data usage on client
    SSL_set_early_data_enabled(ssl_.get(), 1);
#endif
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

bool TLSClientConnection::SetSession(const uint8_t* session_der, size_t session_len) {
    const unsigned char* p = session_der;
    SSL_SESSION* sess = d2i_SSL_SESSION(nullptr, &p, (long)session_len);
    if (!sess) {
        return false;
    }
    bool ok = SSL_set_session(ssl_.get(), sess) == 1;
    SSL_SESSION_free(sess);
    return ok;
}

bool TLSClientConnection::ExportSession(std::string& out_session_der) {
    SSL_SESSION* sess = SSL_get1_session(ssl_.get());
    if (!sess) {
        return false;
    }
    int len = i2d_SSL_SESSION(sess, nullptr);
    if (len <= 0) {
        SSL_SESSION_free(sess);
        return false;
    }
    out_session_der.resize(static_cast<size_t>(len));
    unsigned char* p = reinterpret_cast<unsigned char*>(&out_session_der[0]);
    i2d_SSL_SESSION(sess, &p);
    SSL_SESSION_free(sess);
    return true;
}

}
}
