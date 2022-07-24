#include "quic/crypto/tls_server_connection.h"

namespace quicx {

const SSL_TICKET_AEAD_METHOD TlsServerConnection::_session_ticket_method{
    TlsServerConnection::SessionTicketMaxOverhead,
    TlsServerConnection::SessionTicketSeal,
    TlsServerConnection::SessionTicketOpen,
};

const SSL_PRIVATE_KEY_METHOD TlsServerConnection::_private_key_method{
    &TlsServerConnection::PrivateKeySign,
    nullptr,  // decrypt
    &TlsServerConnection::PrivateKeyComplete,
};

TlsServerConnection::TlsServerConnection(SSL_CTX* ssl_ctx, TlsServerHandlerInterface* tls_handler):
    TlsConnection(ssl_ctx, tls_handler->TlsHandler()),
    _tls_server_handler(tls_handler) {

}

SSLCtxPtr TlsServerConnection::CreateSslCtx(ProofSource* proof_source) {
    // todo
    return nullptr;
}

void TlsServerConnection::SetCertChain(const std::vector<CRYPTO_BUFFER*>& cert_chain) {
    SSL_set_chain_and_key(ssl(), cert_chain.data(), cert_chain.size(), nullptr, &TlsServerConnection::_private_key_method);
}

TlsServerConnection* TlsServerConnection::ConnectionFromSsl(const SSL* ssl) {
    return static_cast<TlsServerConnection*>(TlsConnection::ConnectionFromSsl(ssl));
}

ssl_select_cert_result_t TlsServerConnection::EarlySelectCertCallback(const SSL_CLIENT_HELLO* client_hello) {
    return ConnectionFromSsl(client_hello->ssl)->_tls_server_handler->EarlySelectCertCallback(client_hello);
}

int TlsServerConnection::TlsExtServernameCallback(SSL* ssl, int* out_alert, void* arg) {
    return ConnectionFromSsl(ssl)->_tls_server_handler->TlsExtServernameCallback(out_alert);
}

int TlsServerConnection::SelectAlpnCallback(SSL* ssl,
                    const uint8_t** out,
                    uint8_t* out_len,
                    const uint8_t* in,
                    unsigned in_len,
                    void* arg) {
    return ConnectionFromSsl(ssl)->_tls_server_handler->SelectAlpn(out, out_len, in, in_len);
}

ssl_private_key_result_t TlsServerConnection::PrivateKeySign(SSL* ssl,
                                    uint8_t* out,
                                    size_t* out_len,
                                    size_t max_out,
                                    uint16_t sig_alg,
                                    const uint8_t* in,
                                    size_t in_len) {
    return ConnectionFromSsl(ssl)->_tls_server_handler->PrivateKeySign(out, out_len, max_out, sig_alg, in, in_len);
}

ssl_private_key_result_t TlsServerConnection::PrivateKeyComplete(SSL* ssl,
                                        uint8_t* out,
                                        size_t* out_len,
                                        size_t max_out) {
    return ConnectionFromSsl(ssl)->_tls_server_handler->PrivateKeyComplete(out, out_len, max_out);                                        
}

size_t TlsServerConnection::SessionTicketMaxOverhead(SSL* ssl) {
    return ConnectionFromSsl(ssl)->_tls_server_handler->SessionTicketMaxOverhead();
}

int TlsServerConnection::SessionTicketSeal(SSL* ssl,
                    uint8_t* out,
                    size_t* out_len,
                    size_t max_out_len,
                    const uint8_t* in,
                    size_t in_len) {
    return ConnectionFromSsl(ssl)->_tls_server_handler->SessionTicketSeal(out, out_len, max_out_len, in, in_len);                    
}

enum ssl_ticket_aead_result_t TlsServerConnection::SessionTicketOpen(SSL* ssl,
                                            uint8_t* out,
                                            size_t* out_len,
                                            size_t max_out_len,
                                            const uint8_t* in,
                                            size_t in_len) {
    return ConnectionFromSsl(ssl)->_tls_server_handler->SessionTicketOpen(out, out_len, max_out_len, in, in_len);
}

}