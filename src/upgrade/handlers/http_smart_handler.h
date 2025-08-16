#ifndef UPGRADE_HANDLERS_HTTP_SMART_HANDLER
#define UPGRADE_HANDLERS_HTTP_SMART_HANDLER

#include "upgrade/handlers/base_smart_handler.h"

namespace quicx {
namespace upgrade {

// HTTP Smart Handler for plain text connections
class HttpSmartHandler:
    public BaseSmartHandler {
public:
    explicit HttpSmartHandler(const UpgradeSettings& settings, std::shared_ptr<ITcpAction> tcp_action);

protected:
    // BaseSmartHandler interface
    bool InitializeConnection(std::shared_ptr<ITcpSocket> socket) override;
    int ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) override;
    int WriteData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) override;
    void CleanupConnection(std::shared_ptr<ITcpSocket> socket) override;
    std::string GetType() const override { return "HTTP"; }
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_HTTP_SMART_HANDLER_H 