#ifndef QUIC_CRYPTO_TLS_TLS_CLIENT_CONNECTION
#define QUIC_CRYPTO_TLS_TLS_CLIENT_CONNECTION

#include <string>
#include "quic/crypto/tls/tls_connection.h"

namespace quicx {
namespace quic {

struct SessionInfo {
    uint64_t creation_time;      // Session creation time (Unix timestamp)
    uint32_t timeout;            // Session timeout in seconds
    bool early_data_capable;     // Whether session supports 0-RTT
    std::string server_name;     // Server name (hostname)
};

class TLSClientConnection:
    public TLSConnection {
public:
    TLSClientConnection(std::shared_ptr<TLSCtx> ctx, TlsHandlerInterface* handler);
    ~TLSClientConnection();
    // init ssl connection
    virtual bool Init();

    // do handshake
    virtual bool DoHandleShake();

    // add alpn
    virtual bool AddAlpn(uint8_t* alpn, uint32_t len);

    // set server name (SNI) for TLS handshake
    bool SetServerName(const std::string& server_name);

    // opaque session bytes APIs to hide SSL types from upper layers
    bool SetSession(const uint8_t* session_der, size_t session_len);
    bool ExportSession(std::string& out_session_der, SessionInfo& session_info);

private:
    // Callback to capture TLS 1.3 NewSessionTicket and store per-connection session
    static int NewSessionCallback(SSL* ssl, SSL_SESSION* session);
    void OnNewSession(SSL_SESSION* session);
    SSL_SESSION* StealSavedSession();
    // expose SSL pointer for session get/save by upper layer
    SSL* GetSSL() { return ssl_.get(); }

    // Extract session information from SSL_SESSION
    bool ExtractSessionInfo(SSL_SESSION* session, SessionInfo& info);

    // Get server name from SSL object
    std::string GetServerNameFromSSL(SSL* ssl);
    
private:
    SSL_SESSION* saved_session_ {nullptr};
};

}
}

#endif