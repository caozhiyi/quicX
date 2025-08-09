#include <cstring>

#include "common/log/log.h"
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/core/protocol_detector.h"
#include "upgrade/handlers/https_smart_handler.h"

#include "third/boringssl/include/openssl/ssl.h"
#include "third/boringssl/include/openssl/err.h"
#include "third/boringssl/include/openssl/pem.h"
#include "third/boringssl/include/openssl/x509.h"

namespace quicx {
namespace upgrade {

// SSLContext destructor
SSLContext::~SSLContext() {
    if (ssl) {
        SSL_free(ssl);
        ssl = nullptr;
    }
}

HttpsSmartHandler::HttpsSmartHandler(const UpgradeSettings& settings) 
    : BaseSmartHandler(settings) {
    
    if (!InitializeSSL()) {
        common::LOG_ERROR("Failed to initialize SSL context");
        ssl_ready_ = false;
        // Leave handler constructed but marked not ready
    }
    else {
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
        common::LOG_ERROR("Failed to create SSL context");
        return false;
    }
    
    // Set SSL options
    SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    
    // Set up ALPN
    if (!SetupALPN()) {
        common::LOG_ERROR("Failed to set up ALPN");
        return false;
    }
    
    // Load certificate and private key
    bool cert_loaded = false;
    if (!settings_.cert_file.empty() && !settings_.key_file.empty()) {
        if (SSL_CTX_use_certificate_file(ssl_ctx_, settings_.cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            common::LOG_ERROR("Failed to load certificate file: %s", settings_.cert_file.c_str());
            return false;
        }
        
        if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, settings_.key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            common::LOG_ERROR("Failed to load private key file: %s", settings_.key_file.c_str());
            return false;
        }
        cert_loaded = true;
    } else if (settings_.cert_pem && settings_.key_pem) {
        // Load certificate and key from memory
        BIO* cert_bio = BIO_new_mem_buf(settings_.cert_pem, -1);
        BIO* key_bio = BIO_new_mem_buf(settings_.key_pem, -1);
        
        if (!cert_bio || !key_bio) {
            common::LOG_ERROR("Failed to create BIO for certificate/key");
            if (cert_bio) BIO_free(cert_bio);
            if (key_bio) BIO_free(key_bio);
            return false;
        }
        
        X509* cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
        EVP_PKEY* key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
        
        if (!cert || !key) {
            common::LOG_ERROR("Failed to read certificate/key from memory");
            if (cert) X509_free(cert);
            if (key) EVP_PKEY_free(key);
            BIO_free(cert_bio);
            BIO_free(key_bio);
            return false;
        }
        
        if (SSL_CTX_use_certificate(ssl_ctx_, cert) <= 0) {
            common::LOG_ERROR("Failed to set certificate");
            X509_free(cert);
            EVP_PKEY_free(key);
            BIO_free(cert_bio);
            BIO_free(key_bio);
            return false;
        }
        
        if (SSL_CTX_use_PrivateKey(ssl_ctx_, key) <= 0) {
            common::LOG_ERROR("Failed to set private key");
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
        common::LOG_WARN("No certificate configuration provided; HTTPS features limited for tests");
    }
    
    // Verify certificate and private key match when loaded
    if (cert_loaded) {
        if (SSL_CTX_check_private_key(ssl_ctx_) <= 0) {
            common::LOG_ERROR("Certificate and private key do not match");
            return false;
        }
    }
    
    common::LOG_INFO("SSL context initialized successfully");
    return true;
}

bool HttpsSmartHandler::InitializeConnection(std::shared_ptr<ITcpSocket> socket) {
    if (!ssl_ready_ || !ssl_ctx_) {
        return false;
    }
    // Create SSL context
    SSLContext ssl_ctx(socket);
    ssl_ctx.ssl = SSL_new(ssl_ctx_);
    
    if (!ssl_ctx.ssl) {
        common::LOG_ERROR("Failed to create SSL for new connection");
        return false;
    }
    
    // Set the socket file descriptor
    SSL_set_fd(ssl_ctx.ssl, socket->GetFd());
    
    ssl_context_map_.emplace(socket, std::move(ssl_ctx));
    return true;
}

int HttpsSmartHandler::ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) {
    auto ssl_it = ssl_context_map_.find(socket);
    if (ssl_it == ssl_context_map_.end()) {
        return -1;
    }
    
    SSLContext& ssl_ctx = ssl_it->second;
    
    if (!ssl_ctx.handshake_completed) {
        // Handle SSL handshake
        HandleSSLHandshake(socket);
        return 0; // No data available during handshake
    }
    
    // SSL handshake completed, read and decrypt data
    std::vector<uint8_t> raw_data;
    int bytes_read = socket->Recv(raw_data, 4096); // Read up to 4KB
    
    if (bytes_read <= 0) {
        return bytes_read;
    }
    
    // Resize raw data to actual bytes read
    raw_data.resize(bytes_read);
    
    // Decrypt SSL data
    data.resize(bytes_read);
    int decrypted_len = SSL_read(ssl_ctx.ssl, data.data(), bytes_read);
    
    if (decrypted_len <= 0) {
        int ssl_error = SSL_get_error(ssl_ctx.ssl, decrypted_len);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            // Need more data or can't write, this is normal
            return 0;
        } else {
            common::LOG_ERROR("SSL read error: %d", ssl_error);
            return -1;
        }
    }
    
    // Resize data to actual decrypted length
    data.resize(decrypted_len);
    return decrypted_len;
}

int HttpsSmartHandler::WriteData(std::shared_ptr<ITcpSocket> socket, const std::string& data) {
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
    return SSL_write(ssl_ctx.ssl, data.c_str(), data.length());
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
        return;
    }
    
    SSLContext& ssl_ctx = ssl_it->second;
    
    int ret = SSL_accept(ssl_ctx.ssl);
    if (ret == 1) {
        // SSL handshake completed successfully
        ssl_ctx.handshake_completed = true;
        common::LOG_INFO("SSL handshake completed for socket: %d", socket->GetFd());
        
        // Get ALPN negotiated protocol
        const unsigned char* alpn_protocol;
        unsigned int alpn_len;
        SSL_get0_alpn_selected(ssl_ctx.ssl, &alpn_protocol, &alpn_len);
        if (alpn_protocol && alpn_len > 0) {
            ssl_ctx.negotiated_protocol = std::string(reinterpret_cast<const char*>(alpn_protocol), alpn_len);
            common::LOG_INFO("ALPN negotiated protocol: %s", ssl_ctx.negotiated_protocol.c_str());
        } else {
            common::LOG_INFO("No ALPN protocol negotiated");
        }
        
        // Get client certificate info if available
        X509* client_cert = SSL_get_peer_certificate(ssl_ctx.ssl);
        if (client_cert) {
            char subject[256];
            X509_NAME_oneline(X509_get_subject_name(client_cert), subject, sizeof(subject));
            common::LOG_INFO("Client certificate: %s", subject);
            X509_free(client_cert);
        }
        
        // Process any pending data
        if (!ssl_ctx.pending_data.empty()) {
            // Data will be processed in the next ReadData call
        }
    } else {
        int ssl_error = SSL_get_error(ssl_ctx.ssl, ret);
        if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
            // Handshake in progress, this is normal
            return;
        } else {
            common::LOG_ERROR("SSL handshake failed: %d", ssl_error);
            // Connection will be cleaned up by the base class
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
    // Define supported protocols in order of preference
    // h3 = HTTP/3, h2 = HTTP/2, http/1.1 = HTTP/1.1
    const unsigned char alpn_protocols[] = {
        0x02, 'h', '3',           // h3 (HTTP/3)
        0x02, 'h', '2',           // h2 (HTTP/2) 
        0x08, 'h', 't', 't', 'p', '/', '1', '.', '1'  // http/1.1
    };
    
    // Set ALPN protocols
    if (SSL_CTX_set_alpn_protos(ssl_ctx_, alpn_protocols, sizeof(alpn_protocols)) != 0) {
        common::LOG_ERROR("Failed to set ALPN protocols");
        return false;
    }
    
    // Set ALPN select callback
    SSL_CTX_set_alpn_select_cb(ssl_ctx_, ALPNSelectCallback, this);
    
    common::LOG_INFO("ALPN setup completed");
    return true;
}

int HttpsSmartHandler::ALPNSelectCallback(SSL* ssl, const unsigned char** out, 
                                         unsigned char* outlen, const unsigned char* in, 
                                         unsigned int inlen, void* arg) {
    HttpsSmartHandler* handler = static_cast<HttpsSmartHandler*>(arg);
    
    // Log client's ALPN protocols
    std::string client_protocols;
    for (unsigned int i = 0; i < inlen;) {
        if (i > 0) client_protocols += ", ";
        unsigned char len = in[i++];
        if (i + len <= inlen) {
            client_protocols += std::string(reinterpret_cast<const char*>(&in[i]), len);
            i += len;
        }
    }
    common::LOG_INFO("Client ALPN protocols: %s", client_protocols.c_str());
    
    // Prefer HTTP/3 (h3), then HTTP/2 (h2), then HTTP/1.1
    const char* preferred_protocols[] = {"h3", "h2", "http/1.1"};
    
    for (const char* preferred : preferred_protocols) {
        size_t preferred_len = strlen(preferred);
        
        for (unsigned int i = 0; i < inlen;) {
            unsigned char len = in[i++];
            if (i + len <= inlen) {
                std::string protocol(reinterpret_cast<const char*>(&in[i]), len);
                if (protocol == preferred) {
                    // Found a match, select this protocol
                    *out = &in[i];
                    *outlen = len;
                    common::LOG_INFO("Selected ALPN protocol: %s", preferred);
                    return SSL_TLSEXT_ERR_OK;
                }
                i += len;
            }
        }
    }
    
    // No match found, this will cause the handshake to fail
    common::LOG_WARN("No compatible ALPN protocol found");
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