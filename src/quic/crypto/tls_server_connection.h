#ifndef QUIC_CRYPTO_TLS_SERVER_CONNECTION
#define QUIC_CRYPTO_TLS_SERVER_CONNECTION

#include <memory>
#include <vector>
#include <cstdint>

#include "openssl/ssl.h"
#include "openssl/base.h"
#include "quic/crypto/tls_connection.h"

namespace quicx {

class ProofSource;
/**
 * @brief TLS callback processing interface
 * Boringssl notifies TLS layer operations to the following interfaces
 */
class TlsServerHandlerInterface {
public:
    TlsServerHandlerInterface() {}
    virtual ~TlsServerHandlerInterface() {}

protected:
    // Called from BoringSSL right after SNI is extracted, which is very early
    // in the handshake process.
    virtual enum ssl_select_cert_result_t EarlySelectCertCallback(
        const SSL_CLIENT_HELLO* client_hello) = 0;

    // Called after the ClientHello extensions have been successfully parsed.
    // Returns an SSL_TLSEXT_ERR_* value (see
    // https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#SSL_CTX_set_tlsext_servername_callback).
    //
    // On success, return SSL_TLSEXT_ERR_OK causes the server_name extension to
    // be acknowledged in the ServerHello, or return SSL_TLSEXT_ERR_NOACK which
    // causes it to be not acknowledged.
    //
    // If the function returns SSL_TLSEXT_ERR_ALERT_FATAL, then it puts in
    // |*out_alert| the TLS alert value that the server will send.
    //
    virtual int TlsExtServernameCallback(int* out_alert) = 0;

    // Selects which ALPN to use based on the list sent by the client.
    virtual int SelectAlpn(const uint8_t** out,
                           uint8_t* out_len,
                           const uint8_t* in,
                           unsigned in_len) = 0;

    // Signs |in| using the signature algorithm specified by |sig_alg| (an
    // SSL_SIGN_* value). If the signing operation cannot be completed
    // synchronously, ssl_private_key_retry is returned. If there is an error
    // signing, or if the signature is longer than |max_out|, then
    // ssl_private_key_failure is returned. Otherwise, ssl_private_key_success
    // is returned with the signature put in |*out| and the length in
    // |*out_len|.
    virtual ssl_private_key_result_t PrivateKeySign(uint8_t* out,
                                                    size_t* out_len,
                                                    size_t max_out,
                                                    uint16_t sig_alg,
                                                    const uint8_t* in,
                                                    size_t in_len) = 0;

    // When PrivateKeySign returns ssl_private_key_retry, PrivateKeyComplete
    // will be called after the async sign operation has completed.
    // PrivateKeyComplete puts the resulting signature in |*out| and length in
    // |*out_len|. If the length is greater than |max_out| or if there was an
    // error in signing, then ssl_private_key_failure is returned. Otherwise,
    // ssl_private_key_success is returned.
    virtual ssl_private_key_result_t PrivateKeyComplete(uint8_t* out,
                                                        size_t* out_len,
                                                        size_t max_out) = 0;

    // The following functions are used to implement an SSL_TICKET_AEAD_METHOD.
    // See
    // https://commondatastorage.googleapis.com/chromium-boringssl-docs/ssl.h.html#ssl_ticket_aead_result_t
    // for details on the BoringSSL API.

    // SessionTicketMaxOverhead returns the maximum number of bytes of overhead
    // that SessionTicketSeal may add when encrypting a session ticket.
    virtual size_t SessionTicketMaxOverhead() = 0;

    // SessionTicketSeal encrypts the session ticket in |in|, putting the
    // resulting encrypted ticket in |out|, writing the length of the bytes
    // written to |*out_len|, which is no larger than |max_out_len|. It returns
    // 1 on success and 0 on error.
    virtual int SessionTicketSeal(uint8_t* out,
                                size_t* out_len,
                                size_t max_out_len,
                                const uint8_t* in,
                                size_t in_len) = 0;

    // SessionTicketOpen is called when BoringSSL has an encrypted session
    // ticket |in| and wants the ticket decrypted. This decryption operation can
    // happen synchronously or asynchronously.
    //
    // If the decrypted ticket is not available at the time of the function
    // call, this function returns ssl_ticket_aead_retry. If this function
    // returns ssl_ticket_aead_retry, then SSL_do_handshake will return
    // SSL_ERROR_PENDING_TICKET. Once the pending ticket decryption has
    // completed, SSL_do_handshake needs to be called again.
    //
    // When this function is called and the decrypted ticket is available
    // (either the ticket was decrypted synchronously, or an asynchronous
    // operation has completed and SSL_do_handshake has been called again), the
    // decrypted ticket is put in |out|, and the length of that output is
    // written to |*out_len|, not to exceed |max_out_len|, and
    // ssl_ticket_aead_success is returned. If the ticket cannot be decrypted
    // and should be ignored, this function returns
    // ssl_ticket_aead_ignore_ticket and a full handshake will be performed
    // instead. If a fatal error occurs, ssl_ticket_aead_error can be returned
    // which will terminate the handshake.
    virtual enum ssl_ticket_aead_result_t SessionTicketOpen(
        uint8_t* out,
        size_t* out_len,
        size_t max_out_len,
        const uint8_t* in,
        size_t in_len) = 0;

    // Provides the delegate for callbacks that are shared between client and
    // server.
    virtual TlsHandlerInterface* TlsHandler() = 0;

    friend class TlsServerConnection;
};

class TlsServerConnection:
    public TlsConnection {
public:
    TlsServerConnection(const TlsServerConnection&) = delete;
    TlsServerConnection& operator=(const TlsServerConnection&) = delete;

    TlsServerConnection(SSL_CTX* ssl_ctx, TlsServerHandlerInterface* tls_handler);

    // Creates and configures an SSL_CTX that is appropriate for servers to use.
    static SSLCtxPtr CreateSslCtx(ProofSource* proof_source);

    void SetCertChain(const std::vector<CRYPTO_BUFFER*>& cert_chain);

protected:
    // Specialization of TlsConnection::ConnectionFromSsl.
    static TlsServerConnection* ConnectionFromSsl(const SSL* ssl);

    static ssl_select_cert_result_t EarlySelectCertCallback(const SSL_CLIENT_HELLO* client_hello);

    // These functions are registered as callbacks in BoringSSL and delegate their
    // implementation to the matching methods in Delegate above.
    static int TlsExtServernameCallback(SSL* ssl, int* out_alert, void* arg);
    static int SelectAlpnCallback(SSL* ssl,
                                const uint8_t** out,
                                uint8_t* out_len,
                                const uint8_t* in,
                                unsigned in_len,
                                void* arg);

    // The following functions make up the contents of |kPrivateKeyMethod|.
    static ssl_private_key_result_t PrivateKeySign(SSL* ssl,
                                                 uint8_t* out,
                                                 size_t* out_len,
                                                 size_t max_out,
                                                 uint16_t sig_alg,
                                                 const uint8_t* in,
                                                 size_t in_len);
    static ssl_private_key_result_t PrivateKeyComplete(SSL* ssl,
                                                     uint8_t* out,
                                                     size_t* out_len,
                                                     size_t max_out);

    // The following functions make up the contents of |kSessionTicketMethod|.
    static size_t SessionTicketMaxOverhead(SSL* ssl);
    static int SessionTicketSeal(SSL* ssl,
                               uint8_t* out,
                               size_t* out_len,
                               size_t max_out_len,
                               const uint8_t* in,
                               size_t in_len);
    static enum ssl_ticket_aead_result_t SessionTicketOpen(SSL* ssl,
                                                         uint8_t* out,
                                                         size_t* out_len,
                                                         size_t max_out_len,
                                                         const uint8_t* in,
                                                         size_t in_len);

private:
    // |_private_key_method| is a vtable pointing to PrivateKeySign and
    // PrivateKeyComplete used by the TLS stack to compute the signature for the
    // CertificateVerify message (using the server's private key).
    static const SSL_PRIVATE_KEY_METHOD _private_key_method;

    // Implementation of SSL_TICKET_AEAD_METHOD which delegates to corresponding
    // methods in TlsServerConnection::Delegate (a.k.a. TlsServerHandshaker).
    static const SSL_TICKET_AEAD_METHOD _session_ticket_method;

private:
    TlsServerHandlerInterface* _tls_server_handler;
};

}

#endif