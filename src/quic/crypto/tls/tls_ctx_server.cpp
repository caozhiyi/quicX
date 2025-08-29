#include <cstring>
#include "common/log/log.h"
#include "quic/crypto/tls/tls_ctx_server.h"

namespace quicx {
namespace quic {

TLSServerCtx::TLSServerCtx() {

}

TLSServerCtx::~TLSServerCtx() {
    
}

bool TLSServerCtx::Init(const std::string& cert_file, const std::string& key_file, bool enable_early_data, uint32_t session_ticket_timeout) {
    if (!Init(enable_early_data, session_ticket_timeout)) {
        return false;
    }

    // set cert file
    if (SSL_CTX_use_certificate_chain_file(ssl_ctx_.get(), cert_file.c_str()) != 1) {
        common::LOG_ERROR("SSL_CTX_use_certificate_chain_file failed.");
        return false;
    }

    // set private key file
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx_.get(), key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        common::LOG_ERROR("SSL_CTX_use_PrivateKey_file failed.");
        return false;
    }

    // check private key of certificate
    if (SSL_CTX_check_private_key(ssl_ctx_.get()) != 1) {
        common::LOG_ERROR("SSL_CTX_check_private_key failed.");
        return false;
    }

    return true;
}

bool TLSServerCtx::Init(const char* cert_pem, const char* key_pem, bool enable_early_data, uint32_t session_ticket_timeout) {
    if (!Init(enable_early_data, session_ticket_timeout)) {
        return false;
    }

    BIO* cert_bio = BIO_new_mem_buf(cert_pem, strlen(cert_pem));
    common::CSmartPtr<X509, X509_free> cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
    if (cert == nullptr) {
        common::LOG_ERROR("PEM_read_bio_X509 failed.");
        return false;
    }
    BIO_free(cert_bio);

    BIO* key_bio = BIO_new_mem_buf(key_pem, strlen(key_pem));
    common::CSmartPtr<EVP_PKEY, EVP_PKEY_free> key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
    if (key == nullptr) {
        common::LOG_ERROR("PEM_read_bio_PrivateKey failed.");
        return false;
    }
    BIO_free(key_bio);

    // set cert file
    if (SSL_CTX_use_certificate(ssl_ctx_.get(), cert.get()) != 1) {
        common::LOG_ERROR("SSL_CTX_use_certificate failed.");
        return false;
    }

    // set private key file
    if (SSL_CTX_use_PrivateKey(ssl_ctx_.get(), key.get()) != 1) {
        common::LOG_ERROR("SSL_CTX_use_PrivateKey failed.");
        return false;
    }

    // check private key of certificate
    if (SSL_CTX_check_private_key(ssl_ctx_.get()) != 1) {
        common::LOG_ERROR("SSL_CTX_check_private_key failed.");
        return false;
    }

    return true;
}

bool TLSServerCtx::Init(bool enable_early_data, uint32_t session_ticket_timeout) {
    if (!TLSCtx::Init(enable_early_data)) {
        return false;
    }

    // set server config, disable empty fragments and single ECDH use.
    long ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS)
        | SSL_OP_SINGLE_ECDH_USE;
    SSL_CTX_set_options(ssl_ctx_.get(), ssl_opts);

    // save RAM by releasing read and write buffers when they're empty
    SSL_CTX_set_mode(ssl_ctx_.get(), SSL_MODE_RELEASE_BUFFERS);
    
    // set session ticket timeout for 0-RTT support
    SSL_CTX_set_session_psk_dhe_timeout(ssl_ctx_.get(), session_ticket_timeout);
    return true;
}

}
}