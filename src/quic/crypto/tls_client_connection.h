#ifndef QUIC_CRYPTO_TLS_SERVER_CONNECTION
#define QUIC_CRYPTO_TLS_SERVER_CONNECTION

#include <vector>

#include "openssl/ssl.h"
#include "openssl/base.h"
#include "quic/crypto/tls_connection.h"

namespace quicx {

/**
 * @brief TLS callback processing interface
 * Boringssl notifies TLS layer operations to the following interfaces
 */
class TlsClientHandlerInterface {
public:
    TlsClientHandlerInterface() {}
    virtual ~TlsClientHandlerInterface() {}

protected:
    // Called when a NewSessionTicket is received from the server.
    virtual void InsertSession(bssl::UniquePtr<SSL_SESSION> session) = 0;

    // Provides the delegate for callbacks that are shared between client and
    // server.
    virtual TlsHandlerInterface* TlsHandler() = 0;

    friend class TlsClientConnection;
};

class TlsClientConnection:
    public TlsConnection {
public:
    TlsClientConnection(const TlsClientConnection&) = delete;
    TlsClientConnection& operator=(const TlsClientConnection&) = delete;

    TlsClientConnection(SSL_CTX* ssl_ctx, TlsClientHandlerInterface* tls_handler);

    // Creates and configures an SSL_CTX that is appropriate for clients to use.
    static SSLCtxPtr CreateSslCtx(bool enable_early_data);

protected:
    // Registered as the callback for SSL_CTX_sess_set_new_cb, which calls
    // Delegate::InsertSession.
    static int NewSessionCallback(SSL* ssl, SSL_SESSION* session);

private:
    TlsClientHandlerInterface* _tls_client_handler;
};

}

#endif