#ifndef UPGRADE_CORE_UPGRADE_MANAGER_H
#define UPGRADE_CORE_UPGRADE_MANAGER_H

#include <memory>
#include <unordered_map>
#include "upgrade/include/type.h"
#include "upgrade/core/connection_state.h"
#include "upgrade/network/if_tcp_action.h"
#include "upgrade/network/if_tcp_socket.h"
#include "upgrade/core/version_negotiator.h"

namespace quicx {
namespace upgrade {

// Upgrade manager class responsible for handling protocol upgrades
class UpgradeManager {
public:
    explicit UpgradeManager(const UpgradeSettings& settings);
    ~UpgradeManager() = default;

    // Handle new connection
    void HandleConnection(std::shared_ptr<ITcpSocket> socket);
    
    // Process upgrade for a connection
    void ProcessUpgrade(ConnectionContext& context);
    
    // Handle upgrade failure
    void HandleUpgradeFailure(ConnectionContext& context, const std::string& error);
    
private:
    // Handle HTTP/1.1 to HTTP/3 upgrade
    void HandleHTTP1Upgrade(ConnectionContext& context);
    
    // Handle HTTP/2 to HTTP/3 upgrade
    void HandleHTTP2Upgrade(ConnectionContext& context);
    
    // Handle direct HTTP/3 connection
    void HandleHTTP3Upgrade(ConnectionContext& context);
    
    // Start HTTP/3 connection
    void StartHTTP3Connection(ConnectionContext& context);
    
    // Send HTTP/2 GOAWAY frame
    void SendHTTP2GoAway(ConnectionContext& context);
    
    UpgradeSettings settings_;
    std::unordered_map<std::shared_ptr<ITcpSocket>, ConnectionContext> connections_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_CORE_UPGRADE_MANAGER_H 