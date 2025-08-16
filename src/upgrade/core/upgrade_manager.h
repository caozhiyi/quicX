#ifndef UPGRADE_CORE_UPGRADE_MANAGER
#define UPGRADE_CORE_UPGRADE_MANAGER

#include "upgrade/include/type.h"
#include "upgrade/core/version_negotiator.h"
#include "upgrade/handlers/connection_context.h"

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
    
private:
    // Send upgrade response based on negotiation result
    void SendUpgradeResponse(ConnectionContext& context, const NegotiationResult& result);
    
    // Send failure response
    void SendFailureResponse(ConnectionContext& context, const std::string& error);
    
    UpgradeSettings settings_;
    NegotiationResult last_result_;
};

} // namespace upgrade
} // namespace quicx

#endif // UPGRADE_CORE_UPGRADE_MANAGER_H 