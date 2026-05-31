#include <cerrno>
#include <cstring>
#include <tuple>

#include "common/log/log.h"
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/handlers/https_smart_handler.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

namespace quicx {
namespace upgrade {

// SSLContext destructor
SSLContext::~SSLContext() {
    if (ssl) {
        SSL_free(ssl);
        ssl = nullptr;
    }
}

HttpsSmartHandler::HttpsSmartHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop):
    BaseSmartHandler(settings, event_loop) {
    
    if (!InitializeSSL()) {
        LOG_ERROR("Failed to initialize SSL context");
        ssl_ready_ = false;
        // Leave handler constructed but marked not ready
    } else {
        ssl_ready_ = true;
    }
}

HttpsSmartHandler::~HttpsSmartHandler() {
    // Clean up SSL contexts
    for (auto& pair : ssl_context_map_) {
        CleanupSSL(&pair.second);
    }
    ssl_context_map_.clear();
    
    // Clean up SSL context
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
}

bool HttpsSmartHandler::InitializeSSL() {
    // Initialize SSL library
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    
    // Create SSL context
    ssl_ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx_) {
        LOG_ERROR("Failed to create SSL context");
        return false;
    }
    
    // Set SSL options
    SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    
    // Set up ALPN
    if (!SetupALPN()) {
        LOG_ERROR("Failed to set up ALPN");
        return false;
    }
    
    // Load certificate and private key
    bool cert_loaded = false;
    if (!settings_.cert_file.empty() && !settings_.key_file.empty()) {
        if (SSL_CTX_use_certificate_file(ssl_ctx_, settings_.cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            LOG_ERROR("Failed to load certificate file: %s", settings_.cert_file.c_str());
            return false;
        }
        
        if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, settings_.key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            LOG_ERROR("Failed to load private key file: %s", settings_.key_file.c_str());
            return false;
        }
        cert_loaded = true;

    } else if (settings_.cert_pem && settings_.key_pem) {
        // Load certificate and key from memory
        BIO* cert_bio = BIO_new_mem_buf(settings_.cert_pem, -1);
        BIO* key_bio = BIO_new_mem_buf(settings_.key_pem, -1);
        
        if (!cert_bio || !key_bio) {
            LOG_ERROR("Failed to create BIO for certificate/key");
            if (cert_bio) BIO_free(cert_bio);
            if (key_bio) BIO_free(key_bio);
            return false;
        }
        
        X509* cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
        EVP_PKEY* key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
        
        if (!cert || !key) {
            LOG_ERROR("Failed to read certificate/key from memory");
            if (cert) X509_free(cert);
            if (key) EVP_PKEY_free(key);
            BIO_free(cert_bio);
            BIO_free(key_bio);
            return false;
        }
        
        if (SSL_CTX_use_certificate(ssl_ctx_, cert) <= 0) {
            LOG_ERROR("Failed to set certificate");
            X509_free(cert);
            EVP_PKEY_free(key);
            BIO_free(cert_bio);
            BIO_free(key_bio);
            return false;
        }
        
        if (SSL_CTX_use_PrivateKey(ssl_ctx_, key) <= 0) {
            LOG_ERROR("Failed to set private key");
            X509_free(cert);
            EVP_PKEY_free(key);
            BIO_free(cert_bio);
            BIO_free(key_bio);
            return false;
        }
        
        X509_free(cert);
        EVP_PKEY_free(key);
        BIO_free(cert_bio);
        BIO_free(key_bio);
        cert_loaded = true;

    } else {
        // In tests, we may not have certs. Allow SSL init to continue but handshake will fail later if used.
        LOG_WARN("No certificate configuration provided; HTTPS features limited for tests");
    }
    
    // Verify certificate and private key match when loaded
    if (cert_loaded) {
        if (SSL_CTX_check_private_key(ssl_ctx_) <= 0) {
            LOG_ERROR("Certificate and private key do not match");
            return false;
        }
    }
    
    LOG_INFO("SSL context initialized successfully");
    return true;
}

bool HttpsSmartHandler::InitializeConnection(std::shared_ptr<ITcpSocket> socket) {
    if (!ssl_ready_ || !ssl_ctx_) {
        return false;
    }

    // Construct the SSLContext directly inside the map so its address is
    // stable and its `ssl` member is never owned by a temporary that could
    // be destroyed (and double-free the SSL*) due to copy/move.
    auto emplaced = ssl_context_map_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(socket),
        std::forward_as_tuple(socket));
    if (!emplaced.second) {
        LOG_ERROR("SSLContext already exists for fd=%d", socket->GetFd());
        return false;
    }

    SSLContext& ssl_ctx = emplaced.first->second;
    ssl_ctx.ssl = SSL_new(ssl_ctx_);
    if (!ssl_ctx.ssl) {
        LOG_ERROR("Failed to create SSL for new connection");
        ssl_context_map_.erase(emplaced.first);
        return false;
    }

    // Set the socket file descriptor
    SSL_set_fd(ssl_ctx.ssl, socket->GetFd());
    return true;
}

int HttpsSmartHandler::ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) {
    auto ssl_it = ssl_context_map_.find(socket);
    if (ssl_it == ssl_context_map_.end()) {
        LOG_ERROR("[TLSDBG] ReadData: no SSLContext for fd=%d", socket->GetFd());
        return -1;
    }

    SSLContext& ssl_ctx = ssl_it->second;
    LOG_INFO("[TLSDBG] ReadData fd=%d handshake_completed=%d failed=%d",
             socket->GetFd(), (int)ssl_ctx.handshake_completed,
             (int)ssl_ctx.handshake_failed);

    // If the previous SSL_accept() produced a fatal error (e.g. the client
    // sent plaintext HTTP to the TLS port -- HTTP_REQUEST -- or used an
    // unsupported TLS version), surface it as a hard error so the base
    // layer tears the connection down. Otherwise we'd keep returning 0 on
    // every ET_READ wakeup and spin the event loop forever.
    if (ssl_ctx.handshake_failed) {
        return -1;
    }

    if (!ssl_ctx.handshake_completed) {
        // Drive the TLS handshake forward. OpenSSL has the fd via SSL_set_fd,
        // so it will recv() ClientHello / Finished bytes itself and (when the
        // socket is writable) send ServerHello / EncryptedExtensions back.
        HandleSSLHandshake(socket);
        // Returning 0 means "no application data yet" — the base layer must
        // NOT interpret this as EOF. See BaseSmartHandler::OnRead comment.
        return 0;
    }

    // Handshake completed: the socket fd belongs to OpenSSL via SSL_set_fd,
    // so DO NOT call recv() ourselves -- that would steal encrypted record
    // bytes from OpenSSL and leave SSL_read() permanently starving. Hand the
    // raw fd directly to SSL_read in a small loop and accumulate plaintext.
    constexpr size_t kChunk = 4096;
    data.clear();
    data.reserve(kChunk);

    for (;;) {
        size_t old_size = data.size();
        data.resize(old_size + kChunk);
        int n = SSL_read(ssl_ctx.ssl, data.data() + old_size, kChunk);
        if (n > 0) {
            data.resize(old_size + n);
            // Only one SSL_read iteration per OnRead is enough to make
            // forward progress; we’ll be re-armed by the next ET_READ.
            // Loop only while OpenSSL still has buffered records we can
            // drain without recv()-ing the socket.
            if (SSL_pending(ssl_ctx.ssl) > 0) {
                continue;
            }
            return static_cast<int>(data.size());
        }
        // n <= 0
        data.resize(old_size);
        int ssl_error = SSL_get_error(ssl_ctx.ssl, n);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            // No application data ready. If we already drained some bytes in
            // earlier loop iterations, return them; otherwise tell base layer
            // "wait for next ET_READ".
            return static_cast<int>(data.size());
        }
        if (ssl_error == SSL_ERROR_ZERO_RETURN) {
            // Clean TLS close_notify from peer. Surface as EOF.
            return data.empty() ? -1 : static_cast<int>(data.size());
        }
        LOG_ERROR("SSL_read error: ssl_err=%d", ssl_error);
        return -1;
    }
}

int HttpsSmartHandler::WriteData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) {
    auto ssl_it = ssl_context_map_.find(socket);
    if (ssl_it == ssl_context_map_.end()) {
        return -1;
    }
    
    SSLContext& ssl_ctx = ssl_it->second;
    
    if (!ssl_ctx.handshake_completed) {
        // Continue SSL handshake
        HandleSSLHandshake(socket);
        return 0; // Can't write during handshake
    }
    
    // Encrypt and send data
    return SSL_write(ssl_ctx.ssl, data.data(), data.size());
}

void HttpsSmartHandler::CleanupConnection(std::shared_ptr<ITcpSocket> socket) {
    auto ssl_it = ssl_context_map_.find(socket);
    if (ssl_it != ssl_context_map_.end()) {
        CleanupSSL(&ssl_it->second);
        ssl_context_map_.erase(ssl_it);
    }
}

void HttpsSmartHandler::HandleSSLHandshake(std::shared_ptr<ITcpSocket> socket) {
    auto ssl_it = ssl_context_map_.find(socket);
    if (ssl_it == ssl_context_map_.end()) {
        LOG_ERROR("[TLSDBG] HandleSSLHandshake: no SSLContext for fd=%d", socket->GetFd());
        return;
    }
    
    SSLContext& ssl_ctx = ssl_it->second;
    if (!ssl_ctx.ssl) {
        LOG_ERROR("[TLSDBG] HandleSSLHandshake: SSL* is null for fd=%d", socket->GetFd());
        return;
    }

    // Drain any errors that may already be sitting on the per-thread queue
    // from earlier calls so SSL_get_error()/ERR_peek_error() below report
    // *only* what this SSL_accept() produced.
    ERR_clear_error();
    int ret = SSL_accept(ssl_ctx.ssl);
    int saved_errno = errno;
    LOG_INFO("[TLSDBG] SSL_accept fd=%d ret=%d errno=%d", socket->GetFd(), ret, saved_errno);

    if (ret == 1) {
        // SSL handshake completed successfully
        ssl_ctx.handshake_completed = true;
        LOG_INFO("SSL handshake completed for socket: %d", socket->GetFd());
        
        // Get ALPN negotiated protocol
        const unsigned char* alpn_protocol;
        unsigned int alpn_len;
        SSL_get0_alpn_selected(ssl_ctx.ssl, &alpn_protocol, &alpn_len);
        if (alpn_protocol && alpn_len > 0) {
            ssl_ctx.negotiated_protocol = std::string(reinterpret_cast<const char*>(alpn_protocol), alpn_len);
            LOG_INFO("ALPN negotiated protocol: %s", ssl_ctx.negotiated_protocol.c_str());
        } else {
            LOG_INFO("No ALPN protocol negotiated");
        }
        
        // Get client certificate info if available
        X509* client_cert = SSL_get_peer_certificate(ssl_ctx.ssl);
        if (client_cert) {
            char subject[256];
            X509_NAME_oneline(X509_get_subject_name(client_cert), subject, sizeof(subject));
            LOG_INFO("Client certificate: %s", subject);
            X509_free(client_cert);
        }
        
        // Process any pending data
        if (!ssl_ctx.pending_data.empty()) {
            // Data will be processed in the next ReadData call
        }
    } else {
        int ssl_error = SSL_get_error(ssl_ctx.ssl, ret);
        unsigned long err_q = ERR_peek_error();
        char err_buf[256] = {0};
        if (err_q) ERR_error_string_n(err_q, err_buf, sizeof(err_buf));
        LOG_INFO("[TLSDBG] SSL_accept fd=%d ssl_error=%d err_queue=0x%lx (%s) errno=%d",
                 socket->GetFd(), ssl_error, err_q, err_buf, saved_errno);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            // Handshake in progress, this is normal
            return;
        } else {
            LOG_ERROR("SSL handshake failed: %d", ssl_error);
            // Mark this connection as fatally broken so the next ReadData()
            // call returns -1, which makes BaseSmartHandler::OnRead invoke
            // OnClose() and unregister the fd from the event driver. Without
            // this flag, kqueue/epoll would keep re-firing ET_READ on the
            // same dead fd (e.g. when the client sent plaintext HTTP to the
            // TLS port -- BoringSSL HTTP_REQUEST error -- and immediately
            // closed) and we'd loop on SSL_accept() forever.
            ssl_ctx.handshake_failed = true;
        }
    }
}

void HttpsSmartHandler::CleanupSSL(SSLContext* ssl_ctx) {
    if (ssl_ctx && ssl_ctx->ssl) {
        SSL_shutdown(ssl_ctx->ssl);
        SSL_free(ssl_ctx->ssl);
        ssl_ctx->ssl = nullptr;
    }
}

bool HttpsSmartHandler::SetupALPN() {
    // ALPN protocols advertised by THIS TCP/TLS endpoint.
    //
    // Important: do NOT advertise "h3" here. HTTP/3 lives on QUIC over UDP
    // and never appears as an ALPN value on a TCP/TLS connection. Browsers
    // and curl on TCP send `h2, http/1.1` -- so we must offer those.
    // Discovery of h3 is done out-of-band via the `Alt-Svc` HTTP response
    // header (sent by the H1/H2 path of this upgrade module, or by the
    // real H3 server's responses).
    //
    // Wire format: [len][bytes]...
    //   0x02 'h' '2'                      -> "h2"
    //   0x08 'h' 't' 't' 'p' '/' '1' '.' '1' -> "http/1.1"
    static const unsigned char alpn_protocols[] = {
        0x02, 'h', '2',
        0x08, 'h', 't', 't', 'p', '/', '1', '.', '1',
    };

    // Set ALPN protocols (used by SSL_CTX as the *client*-side advertisement
    // list; harmless on a server context since the server only consults the
    // select callback below).
    if (SSL_CTX_set_alpn_protos(ssl_ctx_, alpn_protocols, sizeof(alpn_protocols)) != 0) {
        LOG_ERROR("Failed to set ALPN protocols");
        return false;
    }

    // Set ALPN select callback (this is what actually matters server-side).
    SSL_CTX_set_alpn_select_cb(ssl_ctx_, ALPNSelectCallback, this);

    LOG_INFO("ALPN setup completed");
    return true;
}

int HttpsSmartHandler::ALPNSelectCallback(SSL* ssl, const unsigned char** out,
                                         unsigned char* outlen, const unsigned char* in,
                                         unsigned int inlen, void* arg) {
    HttpsSmartHandler* handler = static_cast<HttpsSmartHandler*>(arg);
    (void)ssl;
    (void)handler;

    // Log client's ALPN protocols (helps debugging "ALPN mismatch" cases).
    std::string client_protocols;
    for (unsigned int i = 0; i < inlen;) {
        if (i > 0) client_protocols += ", ";
        unsigned char len = in[i++];
        if (i + len <= inlen) {
            client_protocols += std::string(reinterpret_cast<const char*>(&in[i]), len);
            i += len;
        }
    }
    LOG_INFO("Client ALPN protocols: %s", client_protocols.c_str());

    // Server-side preference order.
    //
    // The upgrade module's whole job is to advertise `Alt-Svc: h3=...` so the
    // client switches to HTTP/3 on UDP. That advertisement is much simpler to
    // emit on HTTP/1.1 (a normal `200 OK` with an `Alt-Svc` header) than on
    // HTTP/2 (requires SETTINGS / ACK / HEADERS+HPACK / DATA / GOAWAY frames).
    //
    // So we prefer `http/1.1` whenever the client offers it (browsers and
    // `curl` both do), and fall back to `h2` only for clients that refuse to
    // negotiate H1.1 (nghttp/h2load/some gRPC libs). The `h2` path then sends
    // a minimal but spec-compliant H2 response carrying the same Alt-Svc.
    static const char* const kPreferred[] = { "http/1.1", "h2" };

    for (const char* preferred : kPreferred) {
        size_t preferred_len = strlen(preferred);
        for (unsigned int i = 0; i < inlen;) {
            unsigned char len = in[i++];
            if (i + len > inlen) break;
            if (len == preferred_len &&
                std::memcmp(&in[i], preferred, preferred_len) == 0) {
                *out    = &in[i];
                *outlen = len;
                LOG_INFO("Selected ALPN protocol: %s", preferred);
                return SSL_TLSEXT_ERR_OK;
            }
            i += len;
        }
    }

    // No overlap. Returning NOACK lets the handshake complete without an
    // ALPN extension instead of failing the connection -- this matches what
    // most HTTPS servers do when faced with an unknown client ALPN list and
    // keeps `curl -kv` happy when it offers only `h2,http/1.1` against a
    // mis-configured server.
    LOG_WARN("No compatible ALPN protocol found, proceeding without ALPN");
    return SSL_TLSEXT_ERR_NOACK;
}

std::string HttpsSmartHandler::GetNegotiatedProtocol(std::shared_ptr<ITcpSocket> socket) const {
    auto ssl_it = ssl_context_map_.find(socket);
    if (ssl_it != ssl_context_map_.end()) {
        return ssl_it->second.negotiated_protocol;
    }
    return "";
}

} // namespace upgrade
} // namespace quicx 