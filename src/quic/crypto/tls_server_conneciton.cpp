#include "common/log/log.h"
#include "quic/crypto/tls_server_conneciton.h"

namespace quicx {

TLSServerConnection::TLSServerConnection(SSL_CTX *ctx, std::shared_ptr<TlsHandlerInterface> handler, std::shared_ptr<TlsServerHandlerInterface> ser_handle):
    TLSConnection(ctx, handler),
    _ser_handler(ser_handle) {
}

TLSServerConnection::~TLSServerConnection() {

}

bool TLSServerConnection::Init(const std::string& key_file, const std::string& cert_file) {
    if (!TLSConnection::Init()) {
        return false;
    }
    
    SSL_set_accept_state(_ssl);

    // set private key file
    if (SSL_CTX_use_PrivateKey_file(_ctx, key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        LOG_ERROR("SSL_CTX_use_PrivateKey_file failed.");
        return false;
    }

    // set cert file
    if (SSL_CTX_use_certificate_chain_file(_ctx, cert_file.c_str()) != 1) {
        LOG_ERROR("SSL_CTX_use_certificate_chain_file failed.");
        return false;
    }
    
    // check private key of certificate
    if (SSL_CTX_check_private_key(_ctx) != 1) {
        LOG_ERROR("SSL_CTX_check_private_key failed.");
        return false;
    }

    SSL_CTX_set_alpn_select_cb(_ctx, TLSServerConnection::SSLAlpnSelect, _ssl);

    return true;
}

int TLSServerConnection::SSLAlpnSelect(SSL* ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    TLSServerConnection* conn = (TLSServerConnection*)SSL_get_app_data(ssl);
    
    conn->_ser_handler->SSLAlpnSelect(out, outlen, in, inlen, arg);

    return 0;
}

}