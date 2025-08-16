#ifndef UPGRADE_HANDLERS_BASE_SMART_HANDLER
#define UPGRADE_HANDLERS_BASE_SMART_HANDLER

#include <memory>
#include <unordered_map>

#include "upgrade/include/type.h"
#include "upgrade/core/upgrade_manager.h"
#include "upgrade/network/if_event_driver.h"
#include "upgrade/handlers/if_smart_handler.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {

// Base smart handler containing common logic
class BaseSmartHandler:
    public ISmartHandler {
public:
    explicit BaseSmartHandler(const UpgradeSettings& settings, std::shared_ptr<ITcpAction> tcp_action);
    virtual ~BaseSmartHandler() = default;

    // ISmartHandler interface - common implementations
    void HandleConnect(std::shared_ptr<ITcpSocket> socket) override;
    void HandleRead(std::shared_ptr<ITcpSocket> socket) override;
    void HandleWrite(std::shared_ptr<ITcpSocket> socket) override;
    void HandleClose(std::shared_ptr<ITcpSocket> socket) override;

protected:
    // Virtual methods for subclasses to override
    virtual bool InitializeConnection(std::shared_ptr<ITcpSocket> socket) = 0;
    virtual int ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) = 0;
    virtual int WriteData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) = 0;
    virtual void CleanupConnection(std::shared_ptr<ITcpSocket> socket) = 0;

    // Common helper methods
    void HandleProtocolDetection(std::shared_ptr<ITcpSocket> socket, const std::vector<uint8_t>& data);
    void OnProtocolDetected(ConnectionContext& context);
    void OnUpgradeComplete(ConnectionContext& context);
    void OnUpgradeFailed(ConnectionContext& context, const std::string& error);
    void HandleNegotiationTimeout(std::shared_ptr<ITcpSocket> socket);
    
    // Get negotiated protocol (for HTTPS connections)
    virtual std::string GetNegotiatedProtocol(std::shared_ptr<ITcpSocket> socket) const { return ""; }
    
    // Try to send pending response (handles partial sends)
    void TrySendResponse(ConnectionContext& context);
    
    // Set event driver for registering write events
    void SetEventDriver(std::shared_ptr<IEventDriver> event_driver) { event_driver_ = event_driver; }

protected:
    // Common member variables
    UpgradeSettings settings_;
    std::shared_ptr<UpgradeManager> manager_;
    std::unordered_map<std::shared_ptr<ITcpSocket>, ConnectionContext> connections_;
    std::weak_ptr<ITcpAction> tcp_action_;
    std::weak_ptr<IEventDriver> event_driver_;
};

} // namespace upgrade
} // namespace quicx

#endif