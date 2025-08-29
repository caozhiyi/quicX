#include <cstring>
#include <vector>
#include <openssl/ssl.h>

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

    // Capture NST-generated sessions via new-session callback (client only)
    SSL_CTX_sess_set_new_cb(ctx_->GetSSLCtx(), TLSClientConnection::NewSessionCallback);

    // set connect state, this will start the handshake process.
    SSL_set_connect_state(ssl_.get());
    return true;
}

bool TLSClientConnection::DoHandleShake() {
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

bool TLSClientConnection::AddAlpn(uint8_t* alpn, uint32_t len) {
    if (len == 0 || alpn == nullptr) {
        common::LOG_ERROR("invalid ALPN input");
        return false;
    }
    // Proper ALPN wire format: length-prefixed vector (no NUL terminator)
    std::vector<uint8_t> buf;
    buf.reserve(static_cast<size_t>(len) + 1);
    buf.push_back(static_cast<uint8_t>(len));
    buf.insert(buf.end(), alpn, alpn + len);
    if (SSL_set_alpn_protos(ssl_.get(), buf.data(), static_cast<unsigned int>(buf.size())) != 0) {
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
    
    // Debug: Check session 0-RTT capabilities
    int early_data_capable = SSL_SESSION_early_data_capable(sess);
    common::LOG_DEBUG("Session early_data_capable: %d", early_data_capable);
    
    bool ok = SSL_set_session(ssl_.get(), sess) == 1;
    SSL_SESSION_free(sess);
    return ok;
}

bool TLSClientConnection::ExportSession(std::string& out_session_der, SessionInfo& session_info) {
    // Prefer the saved NST session if present; fallback to current
    SSL_SESSION* saved = StealSavedSession();
    SSL_SESSION* sess = saved ? saved : SSL_get1_session(ssl_.get());
    if (!sess) {
        return false;
    }
    int len = i2d_SSL_SESSION(sess, nullptr);
    if (len <= 0) {
        SSL_SESSION_free(sess);
        return false;
    }

    session_info.server_name = GetServerNameFromSSL(ssl_.get());
    ExtractSessionInfo(sess, session_info);

    out_session_der.resize(static_cast<size_t>(len));
    unsigned char* p = reinterpret_cast<unsigned char*>(&out_session_der[0]);
    i2d_SSL_SESSION(sess, &p);
    SSL_SESSION_free(sess);
    return true;
}

int TLSClientConnection::NewSessionCallback(SSL* ssl, SSL_SESSION* session) {
    TLSClientConnection* conn = (TLSClientConnection*)SSL_get_app_data(ssl);
    if (!conn) {
        common::LOG_ERROR("new session callback failed. ssl:%p", ssl);
        return 0;
    }
    conn->OnNewSession(session);
    // Return 0 so OpenSSL/BoringSSL keeps ownership (we dup if needed)
    return 0;
}

void TLSClientConnection::OnNewSession(SSL_SESSION* session) {
    // Replace any previous saved session
    if (saved_session_) {
        SSL_SESSION_free(saved_session_);
        saved_session_ = nullptr;
    }
    saved_session_ = session;
    SSL_SESSION_up_ref(saved_session_);
}

SSL_SESSION* TLSClientConnection::StealSavedSession() {
    SSL_SESSION* out = saved_session_;
    saved_session_ = nullptr;
    return out;
}

bool TLSClientConnection::ExtractSessionInfo(SSL_SESSION* session, SessionInfo& info) {
    if (!session) {
        return false;
    }
    
    // Extract session information using BoringSSL APIs
    info.creation_time = SSL_SESSION_get_time(session);
    info.timeout = SSL_SESSION_get_timeout(session);
    info.early_data_capable = SSL_SESSION_early_data_capable(session) != 0;
    
    // Note: SSL_SESSION doesn't store server name directly
    // Server name needs to be obtained from SSL object during handshake
    common::LOG_DEBUG("Extracted session info: creation_time=%llu, timeout=%u, early_data_capable=%d",
                     (unsigned long long)info.creation_time, info.timeout, info.early_data_capable);
    
    return true;
}

std::string TLSClientConnection::GetServerNameFromSSL(SSL* ssl) {
    if (!ssl) {
        return "";
    }
    
    // Get server name from SSL object
    const char* server_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
    if (server_name) {
        return std::string(server_name);
    }
    
    // Fallback: try to get from SSL_get_servername_type
    if (SSL_get_servername_type(ssl) == TLSEXT_NAMETYPE_host_name) {
        server_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
        if (server_name) {
            return std::string(server_name);
        }
    }
    
    return "";
}

}
}
