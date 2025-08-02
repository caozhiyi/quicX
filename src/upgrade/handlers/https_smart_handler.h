#ifndef UPGRADE_HANDLERS_HTTPS_SMART_HANDLER_H
#define UPGRADE_HANDLERS_HTTPS_SMART_HANDLER_H

#include "upgrade/handlers/base_smart_handler.h"

// Forward declarations for BoringSSL
struct ssl_st;
struct ssl_ctx_st;

namespace quicx {
namespace upgrade {

// SSL connection context
struct SSLContext {
    std::shared_ptr<ITcpSocket> socket;
    ssl_st* ssl = nullptr;
    bool handshake_completed = false;
    std::vector<uint8_t> pending_data;
    
    SSLContext(std::shared_ptr<ITcpSocket> sock) : socket(sock) {}
    ~SSLContext();
};

// HTTPS Smart Handler for SSL/TLS connections
class HttpsSmartHandler : public BaseSmartHandler {
public:
    explicit HttpsSmartHandler(const UpgradeSettings& settings);
    ~HttpsSmartHandler() override;

protected:
    // BaseSmartHandler interface
    bool InitializeConnection(std::shared_ptr<ITcpSocket> socket) override;
    int ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) override;
    int WriteData(std::shared_ptr<ITcpSocket> socket, const std::string& data) override;
    void CleanupConnection(std::shared_ptr<ITcpSocket> socket) override;
    std::string GetHandlerType() const override { return "HTTPS"; }

private:
    bool InitializeSSL();
    void HandleSSLHandshake(std::shared_ptr<ITcpSocket> socket);
    void CleanupSSL(SSLContext* ssl_ctx);

private:
    ssl_ctx_st* ssl_ctx_ = nullptr;
    std::unordered_map<std::shared_ptr<ITcpSocket>, SSLContext> ssl_context_map_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_HTTPS_SMART_HANDLER_H 