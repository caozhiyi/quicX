// #include <stddef.h>
// #include "openssl/base.h"
// #include "openssl/crypto.h"
// #include "common/log/log.h"
// #include "common/util/singleton.h"
// #include "quic/crypto/tls_connection.h"

// namespace quicx {

// class SslIndexSingleton:
//     public Singleton<SslIndexSingleton> {
// public:
//     int ssl_ex_data_index_connection() const {
//         return ssl_ex_data_index_connection_;
//     }

//     SslIndexSingleton() {
//         CRYPTO_library_init();
//         ssl_ex_data_index_connection_ = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
//         if (ssl_ex_data_index_connection_ < 0) {
//             LOG_ERROR("SSL_get_ex_new_index failed");
//         }
//     }

// private:
//     // The index to supply to SSL_get_ex_data/SSL_set_ex_data for getting/setting
//     // the TlsConnection pointer.
//     int ssl_ex_data_index_connection_;
// };

// const SSL_QUIC_METHOD TlsConnection::_quic_method = {
//     TlsConnection::SetReadSecretCallback,
//     TlsConnection::SetWriteSecretCallback,
//     TlsConnection::WriteMessageCallback,
//     TlsConnection::FlushFlightCallback,
//     TlsConnection::SendAlertCallback,
// };

// TlsConnection::TlsConnection(SSL_CTX* ssl_ctx, TlsHandlerInterface* tls_handler):
//     _tls_handler(tls_handler), _ssl(SSL_new(ssl_ctx)) {
//     if (SSL_set_ex_data(ssl(), SslIndexSingleton::Instance().ssl_ex_data_index_connection(), this) < 0) {
//         LOG_ERROR("SSL_set_ex_data failed");
//     }
// }

// TlsConnection* TlsConnection::ConnectionFromSsl(const SSL* ssl) {
//     return reinterpret_cast<TlsConnection*>(SSL_get_ex_data(ssl, SslIndexSingleton::Instance().ssl_ex_data_index_connection()));
// }

// SSLCtxPtr TlsConnection::CreateSslCtx(int cert_verify_mode) {
//     CRYPTO_library_init();
//     SSLCtxPtr ssl_ctx(SSL_CTX_new(TLS_with_buffers_method()));
//     SSL_CTX_set_min_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
//     SSL_CTX_set_max_proto_version(ssl_ctx.get(), TLS1_3_VERSION);
//     SSL_CTX_set_quic_method(ssl_ctx.get(), &_quic_method);
//     if (cert_verify_mode != SSL_VERIFY_NONE) {
//         SSL_CTX_set_custom_verify(ssl_ctx.get(), cert_verify_mode, &VerifyCallback);
//     }
//     return ssl_ctx;
// }

// EncryptionLevel TlsConnection::QuicEncryptionLevel(enum ssl_encryption_level_t level) {
//     switch (level) {
//     case ssl_encryption_initial:
//         return EL_INITIAL;
//     case ssl_encryption_early_data:
//         return EL_ZERO_RTT;
//     case ssl_encryption_handshake:
//         return EL_HANDSHAKE;
//     case ssl_encryption_application:
//         return EL_FORWARD_SECURE;
//     default:
//         LOG_ERROR("Invalid ssl_encryption_level_t: %d", static_cast<int>(level));
//         return EL_INITIAL;
//     }
// }

// enum ssl_encryption_level_t TlsConnection::BoringEncryptionLevel(EncryptionLevel level) {
//     switch (level) {
//     case EL_INITIAL:
//         return ssl_encryption_initial;
//     case EL_HANDSHAKE:
//         return ssl_encryption_handshake;
//     case EL_ZERO_RTT:
//         return ssl_encryption_early_data;
//     case EL_FORWARD_SECURE:
//         return ssl_encryption_application;
//     default:
//         LOG_ERROR("Invalid encryption level: %d", static_cast<int>(level));
//         return ssl_encryption_initial;
//     }
// }

// enum ssl_verify_result_t TlsConnection::VerifyCallback(SSL* ssl, uint8_t* out_alert) {
//     TlsHandlerInterface* handler = ConnectionFromSsl(ssl)->_tls_handler;
//     return handler->VerifyCert(out_alert);
// }

// int TlsConnection::SetReadSecretCallback(SSL* ssl,
//                                     enum ssl_encryption_level_t level,
//                                     const SSL_CIPHER* cipher,
//                                     const uint8_t* secret,
//                                     size_t secret_len) {
//     std::vector<uint8_t> secret_vec(secret, secret + secret_len);
//     TlsHandlerInterface* handler = ConnectionFromSsl(ssl)->_tls_handler;
//     if (!handler->SetReadSecret(QuicEncryptionLevel(level), cipher,
//                                  secret_vec)) {
//         return 0;
//     }
//     return 1;
// }

// int TlsConnection::SetWriteSecretCallback(SSL* ssl,
//                                     enum ssl_encryption_level_t level,
//                                     const SSL_CIPHER* cipher,
//                                     const uint8_t* secret,
//                                     size_t secret_len) {
//     std::vector<uint8_t> secret_vec(secret, secret + secret_len);
//     TlsHandlerInterface* handler = ConnectionFromSsl(ssl)->_tls_handler;
//     handler->SetWriteSecret(QuicEncryptionLevel(level), cipher, secret_vec);
//     return 1;
// }

// int TlsConnection::WriteMessageCallback(SSL* ssl,
//                                 enum ssl_encryption_level_t level,
//                                 const uint8_t* data,
//                                 size_t len) {
//     TlsHandlerInterface* handler = ConnectionFromSsl(ssl)->_tls_handler;
//     handler->WriteMessage(QuicEncryptionLevel(level), (u_char*)data, len);
//     return 1;
// }

// int TlsConnection::FlushFlightCallback(SSL* ssl) {
//     TlsHandlerInterface* handler = ConnectionFromSsl(ssl)->_tls_handler;
//     handler->FlushFlight();
//     return 1;
// }

// int TlsConnection::SendAlertCallback(SSL* ssl,
//                                 enum ssl_encryption_level_t level,
//                                 uint8_t desc) {
//     TlsHandlerInterface* handler = ConnectionFromSsl(ssl)->_tls_handler;
//     handler->SendAlert(QuicEncryptionLevel(level), desc);
//     return 1;                            
// }

// }