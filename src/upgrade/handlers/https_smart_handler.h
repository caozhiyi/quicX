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
    std::vector<uint8_t> pending_data;
    std::string negotiated_protocol;  // ALPN negotiated protocol
    
    SSLContext(std::shared_ptr<ITcpSocket> sock) : socket(sock) {}
    ~SSLContext();
};

// HTTPS Smart Handler for SSL/TLS connections
class HttpsSmartHandler:
    public BaseSmartHandler {
public:
    explicit HttpsSmartHandler(const UpgradeSettings& settings, std::shared_ptr<ITcpAction> tcp_action);
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