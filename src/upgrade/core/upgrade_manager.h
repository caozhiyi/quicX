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

    // Process upgrade for a connection
    void ProcessUpgrade(ConnectionContext& context);
    
    // Handle upgrade failure
    void HandleUpgradeFailure(ConnectionContext& context, const std::string& error);
    
    // Get upgrade result for external handling
    const NegotiationResult& GetUpgradeResult() const { return last_result_; }
    
    // Get connection context
    ConnectionContext* GetConnectionContext(std::shared_ptr<ITcpSocket> socket);
    
    // Continue sending pending response (called from HandleWrite)
    void ContinueSendResponse(std::shared_ptr<ITcpSocket> socket);
    
    // Add connection context
    void AddConnectionContext(std::shared_ptr<ITcpSocket> socket, const ConnectionContext& context);
    
    // Remove connection context
    void RemoveConnectionContext(std::shared_ptr<ITcpSocket> socket);
    
private:
    // Send upgrade response based on negotiation result
    void SendUpgradeResponse(ConnectionContext& context, const NegotiationResult& result);
    
    // Send failure response
    void SendFailureResponse(ConnectionContext& context, const std::string& error);
    
    // Try to send pending response (handles partial sends)
    void TrySendResponse(ConnectionContext& context);
    
    UpgradeSettings settings_;
    NegotiationResult last_result_;
    std::unordered_map<std::shared_ptr<ITcpSocket>, ConnectionContext> connections_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_CORE_UPGRADE_MANAGER_H 