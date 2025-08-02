#include "common/log/log.h"
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/core/protocol_detector.h"
#include "upgrade/handlers/https_smart_handler.h"

// BoringSSL includes
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
    
    // Load certificate and private key
    if (!settings_.cert_file.empty() && !settings_.key_file.empty()) {
        if (SSL_CTX_use_certificate_file(ssl_ctx_, settings_.cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            common::LOG_ERROR("Failed to load certificate file: {}", settings_.cert_file);
            return false;
        }
        
        if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, settings_.key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
            common::LOG_ERROR("Failed to load private key file: {}", settings_.key_file);
            return false;
        }
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
    } else {
        common::LOG_ERROR("No certificate configuration provided");
        return false;
    }
    
    // Verify certificate and private key match
    if (SSL_CTX_check_private_key(ssl_ctx_) <= 0) {
        common::LOG_ERROR("Certificate and private key do not match");
        return false;
    }
    
    common::LOG_INFO("SSL context initialized successfully");
    return true;
}

bool HttpsSmartHandler::InitializeConnection(std::shared_ptr<ITcpSocket> socket) {
    // Create SSL context
    SSLContext ssl_ctx(socket);
    ssl_ctx.ssl = SSL_new(ssl_ctx_);
    
    if (!ssl_ctx.ssl) {
        common::LOG_ERROR("Failed to create SSL for new connection");
        return false;
    }
    
    // Set the socket file descriptor
    SSL_set_fd(ssl_ctx.ssl, socket->GetFd());
    
    // Store SSL context
    ssl_context_map_[socket] = ssl_ctx;
    
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
            common::LOG_ERROR("SSL read error: {}", ssl_error);
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
        common::LOG_INFO("SSL handshake completed for socket: {}", socket->GetFd());
        
        // Get client certificate info if available
        X509* client_cert = SSL_get_peer_certificate(ssl_ctx.ssl);
        if (client_cert) {
            char subject[256];
            X509_NAME_oneline(X509_get_subject_name(client_cert), subject, sizeof(subject));
            common::LOG_INFO("Client certificate: {}", subject);
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
            common::LOG_ERROR("SSL handshake failed: {}", ssl_error);
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

} // namespace upgrade
} // namespace quicx 