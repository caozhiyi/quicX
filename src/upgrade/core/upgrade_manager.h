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
    
    // Get upgrade result for external handling
    const NegotiationResult& GetUpgradeResult() const { return last_result_; }
    
private:
    // Send upgrade response based on negotiation result
    void SendUpgradeResponse(ConnectionContext& context, const NegotiationResult& result);
    
    // Send failure response
    void SendFailureResponse(ConnectionContext& context, const std::string& error);
    
    UpgradeSettings settings_;
    NegotiationResult last_result_;
    std::unordered_map<std::shared_ptr<ITcpSocket>, ConnectionContext> connections_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_CORE_UPGRADE_MANAGER_H 