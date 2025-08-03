#ifndef UPGRADE_HANDLERS_BASE_SMART_HANDLER_H
#define UPGRADE_HANDLERS_BASE_SMART_HANDLER_H

#include <memory>
#include <unordered_map>
#include "upgrade/handlers/if_smart_handler.h"
#include "upgrade/core/connection_state.h"
#include "upgrade/core/upgrade_manager.h"
#include "upgrade/include/type.h"

namespace quicx {
namespace upgrade {

// Base smart handler containing common logic
class BaseSmartHandler : public ISmartHandler {
public:
    explicit BaseSmartHandler(const UpgradeSettings& settings);
    virtual ~BaseSmartHandler() = default;

    // ISmartHandler interface - common implementations
    void HandleConnect(std::shared_ptr<ITcpSocket> socket, std::shared_ptr<ITcpAction> action) override;
    void HandleRead(std::shared_ptr<ITcpSocket> socket) override;
    void HandleWrite(std::shared_ptr<ITcpSocket> socket) override;
    void HandleClose(std::shared_ptr<ITcpSocket> socket) override;

protected:
    // Virtual methods for subclasses to override
    virtual bool InitializeConnection(std::shared_ptr<ITcpSocket> socket) = 0;
    virtual int ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) = 0;
    virtual int WriteData(std::shared_ptr<ITcpSocket> socket, const std::string& data) = 0;
    virtual void CleanupConnection(std::shared_ptr<ITcpSocket> socket) = 0;
    virtual std::string GetHandlerType() const = 0;

    // Common helper methods
    void HandleProtocolDetection(std::shared_ptr<ITcpSocket> socket, const std::vector<uint8_t>& data);
    void OnProtocolDetected(ConnectionContext& context);
    void OnUpgradeComplete(ConnectionContext& context);
    void OnUpgradeFailed(ConnectionContext& context, const std::string& error);
    
    // Get negotiated protocol (for HTTPS connections)
    virtual std::string GetNegotiatedProtocol(std::shared_ptr<ITcpSocket> socket) const { return ""; }

    // Common member variables
    UpgradeSettings settings_;
    std::shared_ptr<UpgradeManager> manager_;
    std::unordered_map<std::shared_ptr<ITcpSocket>, ConnectionContext> context_map_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_BASE_SMART_HANDLER_H 