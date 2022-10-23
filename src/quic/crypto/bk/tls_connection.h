// #ifndef QUIC_CRYPTO_TLS_CONNECTION
// #define QUIC_CRYPTO_TLS_CONNECTION

// #include <memory>
// #include <vector>
// #include <cstdint>

// #include "openssl/ssl.h"
// #include "quic/crypto/type.h"

// namespace quicx {

// /**
//  * @brief TLS callback processing interface
//  * Boringssl notifies TLS layer operations to the following interfaces
//  */
// class TlsHandlerInterface {
// public:
//     TlsHandlerInterface() {}
//     virtual ~TlsHandlerInterface() {}

// protected:
//     virtual enum ssl_verify_result_t VerifyCert(uint8_t* out_alert) = 0;

//     virtual void SetWriteSecret(EncryptionLevel level,
//                                 const SSL_CIPHER* cipher,
//                                 const std::vector<uint8_t>& write_secret) = 0;
//     virtual bool SetReadSecret(EncryptionLevel level,
//                                const SSL_CIPHER* cipher,
//                                const std::vector<uint8_t>& read_secret) = 0;
//     virtual void WriteMessage(EncryptionLevel level,
//                               u_char* data,
//                               uint32_t len) = 0;
//     virtual void FlushFlight() = 0;
//     virtual void SendAlert(EncryptionLevel level, uint8_t desc) = 0;
//     friend class TlsConnection;     
// };

// class TlsConnection {
// public:
//     TlsConnection(const TlsConnection&) = delete;
//     TlsConnection& operator=(const TlsConnection&) = delete;

//     SSL* ssl() const { return _ssl.get(); }

// protected:
//     TlsConnection(SSL_CTX* ssl_ctx, TlsHandlerInterface* tls_handler);

//     // From a given SSL* |ssl|, returns a pointer to the TlsConnection that it
//     // belongs to. This helper method allows the callbacks set in BoringSSL to be
//     // dispatched to the correct TlsConnection from the SSL* passed into the
//     // callback.
//     static TlsConnection* ConnectionFromSsl(const SSL* ssl);

//     // Creates an SSL_CTX and configures it with the options that are appropriate
//     // for both client and server. The caller is responsible for ownership of the
//     // newly created struct.
//     static SSLCtxPtr CreateSslCtx(int cert_verify_mode);

//     // Functions to convert between BoringSSL's enum ssl_encryption_level_t and
//     // QUIC's EncryptionLevel.
//     static EncryptionLevel QuicEncryptionLevel(enum ssl_encryption_level_t level);
//     static enum ssl_encryption_level_t BoringEncryptionLevel(
//         EncryptionLevel level);

//     static const SSL_QUIC_METHOD _quic_method;
// private:
//     // Registered as the callback for SSL_CTX_set_custom_verify. The
//     // implementation is delegated to Delegate::VerifyCert.
//     static enum ssl_verify_result_t VerifyCallback(SSL* ssl, uint8_t* out_alert);

//     // The following static functions make up the members of kSslQuicMethod:
//     static int SetReadSecretCallback(SSL* ssl,
//                                      enum ssl_encryption_level_t level,
//                                      const SSL_CIPHER* cipher,
//                                      const uint8_t* secret,
//                                      size_t secret_len);

//     static int SetWriteSecretCallback(SSL* ssl,
//                                       enum ssl_encryption_level_t level,
//                                       const SSL_CIPHER* cipher,
//                                       const uint8_t* secret,
//                                       size_t secret_len);

//     static int WriteMessageCallback(SSL* ssl,
//                                     enum ssl_encryption_level_t level,
//                                     const uint8_t* data,
//                                     size_t len);

//     static int FlushFlightCallback(SSL* ssl);

//     static int SendAlertCallback(SSL* ssl,
//                                  enum ssl_encryption_level_t level,
//                                  uint8_t desc);

// private:
//     TlsHandlerInterface* _tls_handler;
//     SSLPtr _ssl;
// };

// }

// #endif