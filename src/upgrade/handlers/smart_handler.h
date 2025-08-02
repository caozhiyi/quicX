#ifndef UPGRADE_HANDLERS_SMART_HANDLER_H
#define UPGRADE_HANDLERS_SMART_HANDLER_H

#include <memory>
#include <unordered_map>
#include "upgrade/include/type.h"
#include "upgrade/core/upgrade_manager.h"
#include "upgrade/core/connection_state.h"
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/network/if_socket_handler.h"


namespace quicx {
namespace upgrade {

// Smart handler that automatically detects and upgrades protocols
class SmartHandler : public ISocketHandler {
public:
    explicit SmartHandler(const UpgradeSettings& settings);
    virtual ~SmartHandler() = default;

    // Handle new connection
    virtual void HandleConnect(std::shared_ptr<ITcpSocket> socket, std::shared_ptr<ITcpAction> action) override;
    
    // Handle data read from socket
    virtual void HandleRead(std::shared_ptr<ITcpSocket> socket) override;
    
    // Handle data write to socket
    virtual void HandleWrite(std::shared_ptr<ITcpSocket> socket) override;
    
    // Handle socket close
    virtual void HandleClose(std::shared_ptr<ITcpSocket> socket) override;

private:
    // Handle protocol detection for new data
    void HandleProtocolDetection(std::shared_ptr<ITcpSocket> socket, const std::vector<uint8_t>& data);
    
    // Called when protocol is detected
    void OnProtocolDetected(ConnectionContext& context);
    
    // Called when upgrade is completed
    void OnUpgradeComplete(ConnectionContext& context);
    
    // Called when upgrade fails
    void OnUpgradeFailed(ConnectionContext& context, const std::string& error);
    
    std::shared_ptr<UpgradeManager> manager_;
    std::unordered_map<std::shared_ptr<ITcpSocket>, ConnectionContext> context_map_;
    UpgradeSettings settings_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_HANDLERS_SMART_HANDLER_H 