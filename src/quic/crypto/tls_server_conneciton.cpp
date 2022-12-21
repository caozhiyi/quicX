#include <vector>
#include <cstring>
#include "common/log/log.h"
#include "quic/crypto/tls_server_conneciton.h"

namespace quicx {

TLSServerConnection::TLSServerConnection(SSL_CTX *ctx, std::shared_ptr<TlsHandlerInterface> handler, std::shared_ptr<TlsServerHandlerInterface> ser_handle):
    TLSConnection(ctx, handler),
    _ser_handler(ser_handle) {
}

TLSServerConnection::~TLSServerConnection() {

}

bool TLSServerConnection::Init() {
    SSL_CTX_set_alpn_select_cb(_ctx, TLSServerConnection::SSLAlpnSelect, nullptr);

    if (!TLSConnection::Init()) {
        return false;
    }
    
    SSL_set_accept_state(_ssl);

    static std::vector<uint8_t> server_quic_early_data_context_ = {2};
    SSL_set_quic_early_data_context(_ssl, server_quic_early_data_context_.data(), server_quic_early_data_context_.size());

    return true;
}

int TLSServerConnection::SSLAlpnSelect(SSL* ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    TLSServerConnection* conn = (TLSServerConnection*)SSL_get_app_data(ssl);
    
    conn->_ser_handler->SSLAlpnSelect(out, outlen, in, inlen, arg);

    return 0;
}

}