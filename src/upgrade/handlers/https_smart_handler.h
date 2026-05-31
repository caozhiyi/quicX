#ifndef UPGRADE_HANDLERS_HTTPS_SMART_HANDLER_H
#define UPGRADE_HANDLERS_HTTPS_SMART_HANDLER_H

#include "upgrade/handlers/base_smart_handler.h"

// Forward declarations for BoringSSL
struct ssl_st;
struct ssl_ctx_st;
// Typedefs to match BoringSSL/OpenSSL common aliases without including ssl.h in header
typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

namespace quicx {
namespace upgrade {

// SSL connection context
struct SSLContext {
    std::shared_ptr<ITcpSocket> socket;
    ssl_st* ssl = nullptr;
    bool handshake_completed = false;
    // Set to true once SSL_accept() has returned a fatal (non-WANT_READ /
    // non-WANT_WRITE) error. The connection is no longer usable; ReadData
    // surfaces this as a hard error so BaseSmartHandler::OnRead can call
    // OnClose() and unregister the fd from the event loop. Without this
    // flag we kept returning 0 on every spurious ET_READ wakeup and the
    // event driver re-fired forever (CPU spin).
    bool handshake_failed = false;
    std::vector<uint8_t> pending_data;
    std::string negotiated_protocol;  // ALPN negotiated protocol

    SSLContext(std::shared_ptr<ITcpSocket> sock) : socket(sock) {}
    ~SSLContext();

    // Non-copyable: SSL* is an owning, non-shareable handle.
    SSLContext(const SSLContext&) = delete;
    SSLContext& operator=(const SSLContext&) = delete;

    // Movable: transfer ownership of the SSL* and reset the source so the
    // moved-from object's destructor doesn't double-free the SSL handle.
    SSLContext(SSLContext&& other) noexcept
        : socket(std::move(other.socket)),
          ssl(other.ssl),
          handshake_completed(other.handshake_completed),
          handshake_failed(other.handshake_failed),
          pending_data(std::move(other.pending_data)),
          negotiated_protocol(std::move(other.negotiated_protocol)) {
        other.ssl = nullptr;
        other.handshake_completed = false;
        other.handshake_failed = false;
    }
    SSLContext& operator=(SSLContext&& other) noexcept {
        if (this != &other) {
            // free our current SSL (if any) before taking ownership of theirs
            if (ssl) {
                // forward-declared SSL_free is invoked from the .cpp via ~SSLContext
                // so we just inline the same logic by calling our destructor path:
                this->~SSLContext();
                new (this) SSLContext(std::move(other));
                return *this;
            }
            socket              = std::move(other.socket);
            ssl                 = other.ssl;
            handshake_completed = other.handshake_completed;
            handshake_failed    = other.handshake_failed;
            pending_data        = std::move(other.pending_data);
            negotiated_protocol = std::move(other.negotiated_protocol);
            other.ssl = nullptr;
            other.handshake_completed = false;
            other.handshake_failed = false;
        }
        return *this;
    }
};

// HTTPS Smart Handler for SSL/TLS connections
class HttpsSmartHandler:
    public BaseSmartHandler {
public:
    explicit HttpsSmartHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop);
    ~HttpsSmartHandler() override;

protected:
    // BaseSmartHandler interface
    bool InitializeConnection(std::shared_ptr<ITcpSocket> socket) override;
    int ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) override;
    int WriteData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) override;
    void CleanupConnection(std::shared_ptr<ITcpSocket> socket) override;
    std::string GetType() const override { return "HTTPS"; }
    
    // Get negotiated ALPN protocol
    std::string GetNegotiatedProtocol(std::shared_ptr<ITcpSocket> socket) const override;

private:
    bool InitializeSSL();
    void HandleSSLHandshake(std::shared_ptr<ITcpSocket> socket);
    void CleanupSSL(SSLContext* ssl_ctx);
    
    // ALPN callback function
    static int ALPNSelectCallback(SSL* ssl, const unsigned char** out, 
                                 unsigned char* outlen, const unsigned char* in, 
                                 unsigned int inlen, void* arg);
    
    // Set up ALPN protocols
    bool SetupALPN();

private:
    SSL_CTX* ssl_ctx_ = nullptr;
    bool ssl_ready_ = false;
    mutable std::unordered_map<std::shared_ptr<ITcpSocket>, SSLContext> ssl_context_map_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_HTTPS_SMART_HANDLER_H 