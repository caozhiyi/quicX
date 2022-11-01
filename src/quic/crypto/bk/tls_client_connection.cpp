// #include "quic/crypto/tls_client_connection.h"

// namespace quicx {

// TlsClientConnection::TlsClientConnection(SSL_CTX* ssl_ctx, TlsClientHandlerInterface* tls_handler):
//     TlsConnection(ssl_ctx, tls_handler->TlsHandler()),
//     _tls_client_handler(tls_handler) {
    
// }

// SSLCtxPtr TlsClientConnection::CreateSslCtx(bool enable_early_data) {
//     SSLCtxPtr ssl_ctx = TlsConnection::CreateSslCtx(SSL_VERIFY_PEER);
//     // Configure certificate verification.
//     int reverify_on_resume_enabled = 1;
//     SSL_CTX_set_reverify_on_resume(ssl_ctx.get(), reverify_on_resume_enabled);
  
//     // Configure session caching.
//     SSL_CTX_set_session_cache_mode(ssl_ctx.get(), SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL);
//     SSL_CTX_sess_set_new_cb(ssl_ctx.get(), NewSessionCallback);
  
//     SSL_CTX_set_early_data_enabled(ssl_ctx.get(), enable_early_data);
//     return ssl_ctx;
// }

// int TlsClientConnection::NewSessionCallback(SSL* ssl, SSL_SESSION* session) {
//     static_cast<TlsClientConnection*>(ConnectionFromSsl(ssl))->_tls_client_handler->InsertSession(bssl::UniquePtr<SSL_SESSION>(session));
//     return 1;
// }

// }
