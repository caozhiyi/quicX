#ifndef UPGRADE_HANDLERS_BASE_SMART_HANDLER
#define UPGRADE_HANDLERS_BASE_SMART_HANDLER

#include <memory>
#include <unordered_map>

#include "upgrade/include/type.h"
#include "common/network/if_event_loop.h"
#include "upgrade/core/upgrade_manager.h"
#include "upgrade/handlers/if_smart_handler.h"
#include "upgrade/handlers/connection_context.h"

namespace quicx {
namespace upgrade {

// Base smart handler containing common logic
class BaseSmartHandler:
    public ISmartHandler {
public:
    explicit BaseSmartHandler(const UpgradeSettings& settings, std::shared_ptr<common::IEventLoop> event_loop);
    virtual ~BaseSmartHandler() = default;

    // ISmartHandler interface - common implementations
    void OnConnect(uint32_t fd) override;
    void OnRead(uint32_t fd) override;
    void OnWrite(uint32_t fd) override;
    void OnError(uint32_t fd) override;
    void OnClose(uint32_t fd) override;

protected:
    // Virtual methods for subclasses to override
    virtual bool InitializeConnection(std::shared_ptr<ITcpSocket> socket) = 0;
    virtual int ReadData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) = 0;
    virtual int WriteData(std::shared_ptr<ITcpSocket> socket, std::vector<uint8_t>& data) = 0;
    virtual void CleanupConnection(std::shared_ptr<ITcpSocket> socket) = 0;

    // Common helper methods
    void HandleProtocolDetection(uint32_t fd, const std::vector<uint8_t>& data);
    void OnProtocolDetected(ConnectionContext& context);
    void OnUpgradeComplete(ConnectionContext& context);
    void OnUpgradeFailed(ConnectionContext& context, const std::string& error);
    void HandleNegotiationTimeout(uint32_t fd);
    
    // Get negotiated protocol (for HTTPS connections)
    virtual std::string GetNegotiatedProtocol(std::shared_ptr<ITcpSocket> socket) const { return ""; }
    
    // Try to send pending response (handles partial sends)
    void TrySendResponse(ConnectionContext& context);

protected:
    // Common member variables
    UpgradeSettings settings_;
    std::shared_ptr<UpgradeManager> manager_;
    std::unordered_map<uint32_t, ConnectionContext> connections_;
    std::weak_ptr<common::IEventLoop> event_loop_;
};

} // namespace upgrade
} // namespace quicx

#endif