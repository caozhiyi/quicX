#include "common/log/log.h"
#include "quic/crypto/tls/tls_server_ctx.h"

namespace quicx {
namespace quic {

TLSServerCtx::TLSServerCtx() {

}

TLSServerCtx::~TLSServerCtx() {
    
}

bool TLSServerCtx::Init(const std::string& cert_file, const std::string& key_file) {
    if (!Init()) {
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

bool TLSServerCtx::Init(X509* cert, EVP_PKEY* key) {
    if (!Init()) {
        return false;
    }

    // set cert file
    if (SSL_CTX_use_certificate(ssl_ctx_.get(), cert) != 1) {
        common::LOG_ERROR("SSL_CTX_use_certificate failed.");
        return false;
    }

    // set private key file
    if (SSL_CTX_use_PrivateKey(ssl_ctx_.get(), key) != 1) {
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

bool TLSServerCtx::Init() {
    if (!TLSCtx::Init()) {
        return false;
    }

    // set server config 
    long ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS)
        | SSL_OP_SINGLE_ECDH_USE;
        
    SSL_CTX_set_options(ssl_ctx_.get(), ssl_opts);

    /* Save RAM by releasing read and write buffers when they're empty */
    SSL_CTX_set_mode(ssl_ctx_.get(), SSL_MODE_RELEASE_BUFFERS);

    return true;
}

}
}