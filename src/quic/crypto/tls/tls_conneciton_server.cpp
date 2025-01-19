#include <vector>
#include <cstring>
#include "common/log/log.h"
#include "quic/crypto/tls/tls_conneciton_server.h"

namespace quicx {
namespace quic {

TLSServerConnection::TLSServerConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler, TlsServerHandlerInterface* ser_handle):
    TLSConnection(ctx, handler),
    ser_handler_(ser_handle) {

}

TLSServerConnection::~TLSServerConnection() {

}

bool TLSServerConnection::Init() {
    if (!TLSConnection::Init()) {
        return false;
    }
    
    SSL_CTX_set_alpn_select_cb(ctx_->GetSSLCtx(), TLSServerConnection::SSLAlpnSelect, nullptr);

    if (!TLSConnection::Init()) {
        return false;
    }
    
    SSL_set_accept_state(ssl_.get());

    static std::vector<uint8_t> server_quic_early_data_context_ = {2};
    SSL_set_quic_early_data_context(ssl_.get(), server_quic_early_data_context_.data(), server_quic_early_data_context_.size());

    return true;
}

int TLSServerConnection::SSLAlpnSelect(SSL* ssl, const unsigned char **out, unsigned char *outlen,
    const unsigned char *in, unsigned int inlen, void *arg) {
    TLSServerConnection* conn = (TLSServerConnection*)SSL_get_app_data(ssl);
    
    conn->ser_handler_->SSLAlpnSelect(out, outlen, in, inlen, arg);

    return 0;
}

}
}