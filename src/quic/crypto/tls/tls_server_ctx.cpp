#include "common/log/log.h"
#include "quic/crypto/tls/tls_server_ctx.h"

namespace quicx {

TLSServerCtx::TLSServerCtx() {

}

TLSServerCtx::~TLSServerCtx() {
    
}

bool TLSServerCtx::Init(const std::string& cert_file, const std::string& key_file) {
    if (!TLSCtx::Init()) {
        return false;
    }

    // set server config 
    long ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS)
        | SSL_OP_SINGLE_ECDH_USE;
        
    SSL_CTX_set_options(_ssl_ctx.get(), ssl_opts);

    /* Save RAM by releasing read and write buffers when they're empty */
    SSL_CTX_set_mode(_ssl_ctx.get(), SSL_MODE_RELEASE_BUFFERS);

    return SetCertificateAndKey(cert_file, key_file);
}

bool TLSServerCtx::SetCertificateAndKey(const std::string& cert_file, const std::string& key_file) {
    /* set private key file */
    if (SSL_CTX_use_PrivateKey_file(_ssl_ctx.get(), key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        LOG_ERROR("SSL_CTX_use_PrivateKey_file failed.");
        return false;
    }

    /* set cert file */
    if (SSL_CTX_use_certificate_chain_file(_ssl_ctx.get(), cert_file.c_str()) != 1) {
        LOG_ERROR("SSL_CTX_use_certificate_chain_file failed.");
        return false;
    }

    /* check private key of certificate */
    if (SSL_CTX_check_private_key(_ssl_ctx.get()) != 1) {
        LOG_ERROR("SSL_CTX_check_private_key failed.");
        return false;
    }

    return true;
}

}