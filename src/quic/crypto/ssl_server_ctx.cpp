#include "common/log/log.h"
#include "quic/crypto/ssl_server_ctx.h"

namespace quicx {

bool SSLServerCtx::Init() {
    if (!SSLCtx::Init()) {
        return false;
    }

    // set server config 
    long ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS)
        | SSL_OP_SINGLE_ECDH_USE;
    SSL_CTX_set_options(_ssl_ctx, ssl_opts);

    /* Save RAM by releasing read and write buffers when they're empty */
    SSL_CTX_set_mode(_ssl_ctx, SSL_MODE_RELEASE_BUFFERS);

    return true;
}

bool SSLServerCtx::SetCertificateAndKey(const std::string& cert_file, const std::string& key_file) {
    /* set private key file */
    if (SSL_CTX_use_PrivateKey_file(_ssl_ctx, key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        LOG_ERROR("SSL_CTX_use_PrivateKey_file failed.");
        return false;
    }

    /* set cert file */
    if (SSL_CTX_use_certificate_chain_file(_ssl_ctx, cert_file.c_str()) != 1) {
        LOG_ERROR("SSL_CTX_use_certificate_chain_file failed.");
        return false;
    }

    /* check private key of certificate */
    if (SSL_CTX_check_private_key(_ssl_ctx) != 1) {
        LOG_ERROR("SSL_CTX_check_private_key failed.");
        return false;
    }

    return true;
}

}